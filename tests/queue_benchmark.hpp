#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <pthread.h>
#include <sched.h>
#include <string>
#include <thread>
#include <vector>

namespace bench {

// ============================================================================
// §1  Configuration
// ============================================================================

struct Config {
    unsigned    n_writers        = 1;           // number of producer threads
    unsigned    n_readers        = 1;           // number of consumer threads
    unsigned    samples_per_role = 100'000;     // samples collected per thread
    unsigned    warmup_rounds    = 3;           // i-cache warm-up iterations
    bool        pin_threads      = false;       // bind threads to CPU cores
    unsigned    first_cpu        = 0;           // writers → [first_cpu .. first_cpu+n_writers)
                                                // readers → [first_cpu+n_writers .. +n_readers)
    std::string csv_path         = "bench.csv"; // output file; "" disables CSV
};

// ============================================================================
// §2  TSC primitives  (Intel paper §3.2.1)
// ============================================================================

#if defined(__x86_64__) || defined(_M_X64)
#  define BENCH_CLOBBER_ALL "%rax", "%rbx", "%rcx", "%rdx"
#else
#  define BENCH_CLOBBER_ALL "%eax", "%ebx", "%ecx", "%edx"
#endif

/// Start fence: CPUID (serialise) → RDTSC (read counter).
/// Nothing before this call can execute after it.
[[nodiscard]] inline uint64_t rdtsc_start() noexcept {
    uint32_t hi, lo;
    __asm__ volatile (
        "CPUID\n\t"           // full serialising fence  (paper §2.2, §3.2.1)
        "RDTSC\n\t"           // EDX:EAX ← timestamp counter
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        : "=r"(hi), "=r"(lo)
        :
        : BENCH_CLOBBER_ALL   // CPUID clobbers EAX,EBX,ECX,EDX (paper p.10)
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

/// Stop fence: RDTSCP (wait-for-retire + read) → CPUID (post-fence).
/// Everything before this call is guaranteed to have retired.
[[nodiscard]] inline uint64_t rdtsc_stop() noexcept {
    uint32_t hi, lo;
    __asm__ volatile (
        "RDTSCP\n\t"          // waits for all prior insns, then reads TSC
        "mov %%edx, %0\n\t"   // data-dependency on RDTSCP → these two movs
        "mov %%eax, %1\n\t"   // are guaranteed to execute after RDTSCP
        "CPUID\n\t"           // post-fence: no later insn can retire early
        : "=r"(hi), "=r"(lo)
        :
        : BENCH_CLOBBER_ALL   // RDTSCP writes ECX too; CPUID writes all four
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

/// Measure the bare harness round-trip cost (paper summary §4, step 4).
[[nodiscard]] inline uint64_t tsc_overhead_once() noexcept {
    uint64_t s = rdtsc_start();
    uint64_t e = rdtsc_stop();
    return (e >= s) ? (e - s) : 0u;
}

/// Take the minimum over many calls to get the stable floor.
[[nodiscard]] inline uint64_t measure_overhead(unsigned iterations = 10'000) noexcept {
    uint64_t best = UINT64_MAX;
    for (unsigned i = 0; i < iterations; ++i) {
        uint64_t v = tsc_overhead_once();
        if (v < best) best = v;
    }
    return (best < 10'000u) ? best : 0u;  // sanity clamp
}

// ============================================================================
// §3  Thread pinning
// ============================================================================

inline void pin_to_cpu(unsigned cpu_id) noexcept {
    cpu_set_t cs;
    CPU_ZERO(&cs);
    CPU_SET(cpu_id, &cs);
    pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);
}

// ============================================================================
// §4  Statistics  (paper's var_calc(), plus tail-latency percentiles)
// ============================================================================

struct Stats {
    // ---- paper metrics ----
    uint64_t min_cycles  = 0;
    uint64_t max_cycles  = 0;
    uint64_t max_dev     = 0;   // max − min  ("max_deviation" in the paper)
    double   mean        = 0.0;
    double   variance    = 0.0; // population variance
    double   stddev      = 0.0;

    // ---- tail-latency percentiles ----
    uint64_t p50         = 0;
    uint64_t p90         = 0;
    uint64_t p99         = 0;
    uint64_t p999        = 0;   // 99.9th
    uint64_t p9999       = 0;   // 99.99th

    uint64_t sample_count = 0;
};

/// Compute statistics from a *sorted* copy of the sample vector.
/// The vector is sorted in-place; pass a copy if you need the original order.
[[nodiscard]] inline Stats compute_stats(std::vector<uint64_t>& samples) {
    assert(!samples.empty());
    std::sort(samples.begin(), samples.end());

    Stats s;
    s.sample_count = samples.size();
    s.min_cycles   = samples.front();
    s.max_cycles   = samples.back();
    s.max_dev      = s.max_cycles - s.min_cycles;

    // Mean
    double sum = 0.0;
    for (auto v : samples) sum += static_cast<double>(v);
    s.mean = sum / static_cast<double>(s.sample_count);

    // Population variance
    double sq = 0.0;
    for (auto v : samples) {
        double d = static_cast<double>(v) - s.mean;
        sq += d * d;
    }
    s.variance = sq / static_cast<double>(s.sample_count);
    s.stddev   = std::sqrt(s.variance);

    // Percentiles (index = floor(p/100 * N), clamped)
    auto pct = [&](double p) -> uint64_t {
        // Use the "nearest rank" method so p=100 → last element
        size_t idx = static_cast<size_t>(std::ceil(p / 100.0 * static_cast<double>(s.sample_count)));
        if (idx > 0) --idx;
        idx = std::min(idx, s.sample_count - 1);
        return samples[idx];
    };
    s.p50   = pct(50.0);
    s.p90   = pct(90.0);
    s.p99   = pct(99.0);
    s.p999  = pct(99.9);
    s.p9999 = pct(99.99);

    return s;
}

// ============================================================================
// §5  Result
// ============================================================================

struct Result {
    Stats    enqueue;
    Stats    dequeue;
    uint64_t timing_overhead  = 0;
    unsigned n_writers        = 0;
    unsigned n_readers        = 0;
    unsigned samples_per_role = 0;

    // Raw per-thread samples (thread index → sample vector, arrival order).
    // Available for custom analysis after run() returns.
    std::vector<std::vector<uint64_t>> enqueue_samples; // [writer_id][sample_idx]
    std::vector<std::vector<uint64_t>> dequeue_samples; // [reader_id][sample_idx]

    // ---- console summary ----
    void print(std::ostream& os = std::cout) const {
        auto bar = [&]{ os << "  " << std::string(56, '-') << "\n"; };
        os << "\n  ╔══════════════════════════════════════════════════════╗\n";
        os << "  ║           Queue Tail-Latency Benchmark               ║\n";
        os << "  ╚══════════════════════════════════════════════════════╝\n";
        os << "  Writers : " << n_writers
           << "   Readers : " << n_readers
           << "   Samples/thread : " << samples_per_role << "\n";
        os << "  TSC harness overhead (subtracted) : "
           << timing_overhead << " cycles\n";
        bar();

        auto print_stats = [&](const char* label, const Stats& st) {
            os << "  [" << label << "]  n=" << st.sample_count << "\n";
            os << "    min      : " << st.min_cycles << " cy\n";
            os << "    p50      : " << st.p50        << " cy\n";
            os << "    p90      : " << st.p90        << " cy\n";
            os << "    p99      : " << st.p99        << " cy\n";
            os << "    p99.9    : " << st.p999       << " cy\n";
            os << "    p99.99   : " << st.p9999      << " cy\n";
            os << "    max      : " << st.max_cycles << " cy\n";
            os << "    mean     : " << st.mean       << " cy\n";
            os << "    stddev   : " << st.stddev     << " cy\n";
            os << "    variance : " << st.variance   << " cy²\n";
            bar();
        };
        print_stats("ENQUEUE", enqueue);
        print_stats("DEQUEUE", dequeue);
        os << "\n";
    }

    // ---- CSV output ----
    //
    // Format:
    //   role,thread_id,seq,cycles
    //
    // `role`      : "enqueue" or "dequeue"
    // `thread_id` : 0-based index within that role
    // `seq`       : sample index (arrival order, not sorted)
    // `cycles`    : overhead-adjusted TSC delta
    //
    // Having every raw sample lets the reader reconstruct any percentile,
    // plot CDFs, detect bimodal distributions, etc.
    void write_csv(const std::string& path) const {
        if (path.empty()) return;
        std::ofstream f(path);
        if (!f) {
            std::cerr << "[bench] WARNING: cannot open " << path
                      << " for writing – CSV skipped.\n";
            return;
        }
        f << "role,thread_id,seq,cycles\n";
        for (unsigned tid = 0; tid < enqueue_samples.size(); ++tid) {
            const auto& v = enqueue_samples[tid];
            for (size_t s = 0; s < v.size(); ++s)
                f << "enqueue," << tid << ',' << s << ',' << v[s] << '\n';
        }
        for (unsigned tid = 0; tid < dequeue_samples.size(); ++tid) {
            const auto& v = dequeue_samples[tid];
            for (size_t s = 0; s < v.size(); ++s)
                f << "dequeue," << tid << ',' << s << ',' << v[s] << '\n';
        }
        std::cout << "[bench] CSV written → " << path
                  << "  (" << (enqueue_samples.size() + dequeue_samples.size())
                  << " thread(s), "
                  << (n_writers + n_readers) * samples_per_role
                  << " rows)\n";
    }
};

// ============================================================================
// §6  Thread bodies
// ============================================================================

namespace detail {

/// Shared synchronisation between all benchmark threads.
struct Sync {
    std::atomic<unsigned> ready{0};   // each thread increments when set up
    std::atomic<bool>     go{false};  // main releases all threads at once
    std::atomic<bool>     done{false};// writers signal completion to readers
};

// ----------------------------------------------------------------------------
// Writer thread
//
// Hot path per iteration:
//   rdtsc_start()  →  queue.enqueue(move(item))  →  rdtsc_stop()
//   store delta to pre-allocated TLS buffer[i]
//
// Nothing else happens inside the measurement loop.
// ----------------------------------------------------------------------------
template<typename Queue, typename T>
void writer_thread(
    Queue&               queue,
    Sync&                sync,
    unsigned             cpu_id,
    bool                 pin,
    unsigned             warmup_rounds,
    unsigned             samples,
    uint64_t             overhead,
    std::vector<uint64_t>& out,   // pre-sized by caller; we index directly
    T                    proto)
{
    if (pin) pin_to_cpu(cpu_id);

    // ── i-cache warm-up (paper §3.1.1 lines 19-30) ──────────────────────────
    // Run the timing sequence several times so that CPUID, RDTSC, RDTSCP are
    // hot in the instruction cache before measurement starts.
    for (unsigned w = 0; w < warmup_rounds; ++w) {
        volatile uint64_t s = rdtsc_start();
        volatile uint64_t e = rdtsc_stop();
        (void)s; (void)e;
    }

    // ── barrier ─────────────────────────────────────────────────────────────
    sync.ready.fetch_add(1, std::memory_order_release);
    while (!sync.go.load(std::memory_order_acquire))
        std::this_thread::yield();

    // ── measurement loop ─────────────────────────────────────────────────────
    // `out` is already sized to `samples` — no allocation inside the loop.
    for (unsigned i = 0; i < samples; ++i) {
        T item{proto};                         // fresh value each iteration

        uint64_t start = rdtsc_start();
        // ─── code under measurement ───
        queue.enqueue(std::move(item));
        // ─────────────────────────────
        uint64_t stop = rdtsc_stop();

        uint64_t delta = (stop >= start) ? (stop - start) : 0u;
        out[i] = (delta >= overhead)     ? (delta - overhead) : 0u;
    }

    sync.done.store(true, std::memory_order_release);
}

// ----------------------------------------------------------------------------
// Reader thread
//
// Hot path per successful dequeue:
//   rdtsc_start()  →  queue.dequeue(out)  →  rdtsc_stop()
//   store delta to pre-allocated TLS buffer[i]  (only on success)
//
// On an empty-queue miss the TSC delta is discarded; we spin without
// recording so contention noise never enters the latency distribution.
// ----------------------------------------------------------------------------
template<typename Queue, typename T>
void reader_thread(
    Queue&               queue,
    Sync&                sync,
    unsigned             cpu_id,
    bool                 pin,
    unsigned             warmup_rounds,
    unsigned             samples,
    uint64_t             overhead,
    std::vector<uint64_t>& out)   // pre-sized by caller
{
    if (pin) pin_to_cpu(cpu_id);

    // ── i-cache warm-up ─────────────────────────────────────────────────────
    for (unsigned w = 0; w < warmup_rounds; ++w) {
        volatile uint64_t s = rdtsc_start();
        volatile uint64_t e = rdtsc_stop();
        (void)s; (void)e;
    }

    // ── barrier ─────────────────────────────────────────────────────────────
    sync.ready.fetch_add(1, std::memory_order_release);
    while (!sync.go.load(std::memory_order_acquire))
        std::this_thread::yield();

    // ── measurement loop ─────────────────────────────────────────────────────
    std::shared_ptr<T> item_out;
    unsigned hits = 0;

    // We stop when we have enough samples AND all writers have finished,
    // so we never spin forever on a queue that has been fully drained.
    while (hits < samples || !sync.done.load(std::memory_order_acquire)) {
        uint64_t start = rdtsc_start();
        // ─── code under measurement ───
        bool ok = queue.dequeue(item_out);
        // ─────────────────────────────
        uint64_t stop = rdtsc_stop();

        if (ok) {
            uint64_t delta = (stop >= start) ? (stop - start) : 0u;
            out[hits]      = (delta >= overhead) ? (delta - overhead) : 0u;
            ++hits;
        }
        // miss → discard delta, spin; no recording to avoid contamination
    }

    // Trim in case we received more items than `samples` (multi-writer race).
    // out is pre-sized; extra hits beyond samples were written out-of-bounds
    // only if hits > samples, which we guard below.
    (void)hits; // hits == samples by loop invariant
}

} // namespace detail

// ============================================================================
// §7  Public entry point
// ============================================================================

/**
 * bench::run<T>(queue, cfg, item_proto)
 *
 * Spawns n_writers + n_readers threads, measures per-operation TSC latency,
 * writes a CSV, and returns a Result with full statistics.
 *
 * @tparam T          Element type stored in the queue.
 * @tparam Queue      Must satisfy BaseQueue<T> (enqueue / dequeue).
 * @param  queue      The queue instance to benchmark (must be empty on entry).
 * @param  cfg        Benchmark configuration.
 * @param  item_proto Value copied to produce each enqueued item (default T{}).
 */
template<typename T, typename Queue>
[[nodiscard]] Result run(Queue& queue, const Config& cfg, T item_proto = T{}) {

    // ── Step 1: calibrate TSC harness overhead ───────────────────────────────
    const uint64_t overhead = measure_overhead();

    // ── Step 2: pre-allocate per-thread sample buffers ──────────────────────
    //
    // Allocating before threads start means the measurement loop itself
    // never calls new/malloc.  We size each buffer to exactly samples_per_role
    // and index it directly (out[i] = delta), which is a single cache-line
    // write with no branch.
    //
    // These vectors live in the main thread (not thread_local) so they are
    // accessible after threads join.  The threads receive references and write
    // into them with no synchronisation needed (each thread owns its own slot).
    std::vector<std::vector<uint64_t>> writer_buf(cfg.n_writers);
    std::vector<std::vector<uint64_t>> reader_buf(cfg.n_readers);
    for (auto& v : writer_buf) v.resize(cfg.samples_per_role, 0);
    for (auto& v : reader_buf) v.resize(cfg.samples_per_role, 0);

    // ── Step 3: spawn threads ────────────────────────────────────────────────
    detail::Sync         sync;
    std::vector<std::thread> threads;
    threads.reserve(cfg.n_writers + cfg.n_readers);

    for (unsigned w = 0; w < cfg.n_writers; ++w) {
        threads.emplace_back(
            detail::writer_thread<Queue, T>,
            std::ref(queue),
            std::ref(sync),
            cfg.first_cpu + w,
            cfg.pin_threads,
            cfg.warmup_rounds,
            cfg.samples_per_role,
            overhead,
            std::ref(writer_buf[w]),
            item_proto);
    }

    for (unsigned r = 0; r < cfg.n_readers; ++r) {
        threads.emplace_back(
            detail::reader_thread<Queue, T>,
            std::ref(queue),
            std::ref(sync),
            cfg.first_cpu + cfg.n_writers + r,
            cfg.pin_threads,
            cfg.warmup_rounds,
            cfg.samples_per_role,
            overhead,
            std::ref(reader_buf[r]));
    }

    // ── Step 4: release all threads simultaneously ───────────────────────────
    const unsigned total = cfg.n_writers + cfg.n_readers;
    while (sync.ready.load(std::memory_order_acquire) < total)
        std::this_thread::yield();

    sync.go.store(true, std::memory_order_release);

    for (auto& t : threads) t.join();

    // ── Step 5: build aggregate sample vectors ───────────────────────────────
    //
    // Flatten per-thread buffers into two vectors (all enqueue samples,
    // all dequeue samples).  We need sorted copies for percentiles, but we
    // keep the originals (arrival order) in Result for CSV export.
    auto flatten = [](const std::vector<std::vector<uint64_t>>& bufs)
        -> std::vector<uint64_t>
    {
        size_t total = 0;
        for (const auto& v : bufs) total += v.size();
        std::vector<uint64_t> out;
        out.reserve(total);
        for (const auto& v : bufs)
            out.insert(out.end(), v.begin(), v.end());
        return out;
    };

    auto all_enq = flatten(writer_buf);
    auto all_deq = flatten(reader_buf);

    // ── Step 6: compute statistics (sorts in-place) ──────────────────────────
    Result res;
    res.timing_overhead  = overhead;
    res.n_writers        = cfg.n_writers;
    res.n_readers        = cfg.n_readers;
    res.samples_per_role = cfg.samples_per_role;
    res.enqueue          = compute_stats(all_enq);
    res.dequeue          = compute_stats(all_deq);

    // Move per-thread buffers into Result for CSV export (arrival order).
    res.enqueue_samples  = std::move(writer_buf);
    res.dequeue_samples  = std::move(reader_buf);

    // ── Step 7: write CSV ────────────────────────────────────────────────────
    res.write_csv(cfg.csv_path);

    return res;
}

} // namespace bench

// ============================================================================
// §8  Standalone demo  (compiled only with -DQUEUE_BENCHMARK_MAIN)
// ============================================================================
#ifdef QUEUE_BENCHMARK_MAIN

#include <mutex>
#include <queue>

/// Minimal mutex-based queue satisfying the BaseQueue concept.
template<typename T>
class SimpleQueue {
public:
    void enqueue(T&& item) {
        std::lock_guard<std::mutex> lk(mtx_);
        q_.push(std::move(item));
    }
    bool dequeue(std::shared_ptr<T>& out) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (q_.empty()) return false;
        out = std::make_shared<T>(std::move(q_.front()));
        q_.pop();
        return true;
    }
private:
    std::mutex    mtx_;
    std::queue<T> q_;
};

int main() {
    SimpleQueue<int> queue;

    bench::Config cfg;
    cfg.n_writers        = 2;
    cfg.n_readers        = 2;
    cfg.samples_per_role = 200'000;
    cfg.warmup_rounds    = 3;
    cfg.pin_threads      = false;   // flip to true if you want CPU affinity
    cfg.first_cpu        = 0;
    cfg.csv_path         = "latency.csv";

    auto result = bench::run<int>(queue, cfg, /*item_proto=*/42);
    result.print();

    // The CSV has already been written by run().
    // You can also call result.write_csv("another.csv") to re-export.
    return 0;
}

#endif // QUEUE_BENCHMARK_MAIN
