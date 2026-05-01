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

struct Config {
    unsigned    n_writers        = 1;
    unsigned    n_readers        = 1;
    unsigned    samples_per_role = 100'000;
    unsigned    warmup_rounds    = 3;
    bool        pin_threads      = false;
    unsigned    first_cpu        = 0;
    std::string csv_path         = "bench.csv";
};

#if defined(__x86_64__) || defined(_M_X64)
#  define BENCH_CLOBBER_ALL "%rax", "%rbx", "%rcx", "%rdx"
#else
#  define BENCH_CLOBBER_ALL "%eax", "%ebx", "%ecx", "%edx"
#endif

[[nodiscard]] inline uint64_t rdtsc_start() noexcept {
    uint32_t hi, lo;
    __asm__ volatile (
        "CPUID\n\t"
        "RDTSC\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        : "=r"(hi), "=r"(lo)
        :
        : BENCH_CLOBBER_ALL
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

[[nodiscard]] inline uint64_t rdtsc_stop() noexcept {
    uint32_t hi, lo;
    __asm__ volatile (
        "RDTSCP\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        "CPUID\n\t"
        : "=r"(hi), "=r"(lo)
        :
        : BENCH_CLOBBER_ALL
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

[[nodiscard]] inline uint64_t tsc_overhead_once() noexcept {
    uint64_t s = rdtsc_start();
    uint64_t e = rdtsc_stop();
    return (e >= s) ? (e - s) : 0u;
}

[[nodiscard]] inline uint64_t measure_overhead(unsigned iterations = 10'000) noexcept {
    uint64_t best = UINT64_MAX;
    for (unsigned i = 0; i < iterations; ++i) {
        uint64_t v = tsc_overhead_once();
        if (v < best) best = v;
    }
    return (best < 10'000u) ? best : 0u;
}

inline void pin_to_cpu(unsigned cpu_id) noexcept {
    cpu_set_t cs;
    CPU_ZERO(&cs);
    CPU_SET(cpu_id, &cs);
    pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);
}

struct Stats {
    uint64_t min_cycles  = 0;
    uint64_t max_cycles  = 0;
    uint64_t max_dev     = 0;
    double   mean        = 0.0;
    double   variance    = 0.0;
    double   stddev      = 0.0;
    uint64_t p50         = 0;
    uint64_t p90         = 0;
    uint64_t p99         = 0;
    uint64_t p999        = 0;
    uint64_t p9999       = 0;
    uint64_t sample_count = 0;
};

[[nodiscard]] inline Stats compute_stats(std::vector<uint64_t>& samples) {
    assert(!samples.empty());
    std::sort(samples.begin(), samples.end());

    Stats s;
    s.sample_count = samples.size();
    s.min_cycles   = samples.front();
    s.max_cycles   = samples.back();
    s.max_dev      = s.max_cycles - s.min_cycles;

    double sum = 0.0;
    for (auto v : samples) sum += static_cast<double>(v);
    s.mean = sum / static_cast<double>(s.sample_count);

    double sq = 0.0;
    for (auto v : samples) {
        double d = static_cast<double>(v) - s.mean;
        sq += d * d;
    }
    s.variance = sq / static_cast<double>(s.sample_count);
    s.stddev   = std::sqrt(s.variance);

    auto pct = [&](double p) -> uint64_t {
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

struct Result {
    Stats    enqueue;
    Stats    dequeue;
    uint64_t timing_overhead  = 0;
    unsigned n_writers        = 0;
    unsigned n_readers        = 0;
    unsigned samples_per_role = 0;

    std::vector<std::vector<uint64_t>> enqueue_samples;
    std::vector<std::vector<uint64_t>> dequeue_samples;

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

namespace detail {

struct Sync {
    std::atomic<unsigned> ready{0};
    std::atomic<bool>     go{false};
    std::atomic<bool>     done{false};
};

template<typename Queue, typename T>
void writer_thread(
    Queue& queue, Sync& sync,
    unsigned cpu_id, bool pin,
    unsigned warmup_rounds, unsigned samples,
    uint64_t overhead,
    std::vector<uint64_t>& out,
    T proto)
{
    if (pin) pin_to_cpu(cpu_id);

    for (unsigned w = 0; w < warmup_rounds; ++w) {
        volatile uint64_t s = rdtsc_start();
        volatile uint64_t e = rdtsc_stop();
        (void)s; (void)e;
    }

    sync.ready.fetch_add(1, std::memory_order_release);
    while (!sync.go.load(std::memory_order_acquire))
        std::this_thread::yield();

    for (unsigned i = 0; i < samples; ++i) {
        T item{proto};
        uint64_t start = rdtsc_start();
        queue.enqueue(std::move(item));
        uint64_t stop = rdtsc_stop();
        uint64_t delta = (stop >= start) ? (stop - start) : 0u;
        out[i] = (delta >= overhead)     ? (delta - overhead) : 0u;
    }

    sync.done.store(true, std::memory_order_release);
}

template<typename Queue, typename T>
void reader_thread(
    Queue& queue, Sync& sync,
    unsigned cpu_id, bool pin,
    unsigned warmup_rounds, unsigned samples,
    uint64_t overhead,
    std::vector<uint64_t>& out)
{
    if (pin) pin_to_cpu(cpu_id);

    for (unsigned w = 0; w < warmup_rounds; ++w) {
        volatile uint64_t s = rdtsc_start();
        volatile uint64_t e = rdtsc_stop();
        (void)s; (void)e;
    }

    sync.ready.fetch_add(1, std::memory_order_release);
    while (!sync.go.load(std::memory_order_acquire))
        std::this_thread::yield();

    std::shared_ptr<T> item_out;
    unsigned hits = 0;

    while (hits < samples || !sync.done.load(std::memory_order_acquire)) {
        uint64_t start = rdtsc_start();
        bool ok = queue.dequeue(item_out);
        uint64_t stop = rdtsc_stop();

        if (ok) {
            uint64_t delta = (stop >= start) ? (stop - start) : 0u;
            out[hits]      = (delta >= overhead) ? (delta - overhead) : 0u;
            ++hits;
        }
    }

    (void)hits;
}

} // namespace detail

template<typename T, typename Queue>
[[nodiscard]] Result run(Queue& queue, const Config& cfg, T item_proto = T{}) {

    const uint64_t overhead = measure_overhead();

    std::vector<std::vector<uint64_t>> writer_buf(cfg.n_writers);
    std::vector<std::vector<uint64_t>> reader_buf(cfg.n_readers);
    for (auto& v : writer_buf) v.resize(cfg.samples_per_role, 0);
    for (auto& v : reader_buf) v.resize(cfg.samples_per_role, 0);

    detail::Sync sync;
    std::vector<std::thread> threads;
    threads.reserve(cfg.n_writers + cfg.n_readers);

    for (unsigned w = 0; w < cfg.n_writers; ++w) {
        threads.emplace_back(
            detail::writer_thread<Queue, T>,
            std::ref(queue), std::ref(sync),
            cfg.first_cpu + w, cfg.pin_threads,
            cfg.warmup_rounds, cfg.samples_per_role,
            overhead, std::ref(writer_buf[w]),
            item_proto);
    }

    for (unsigned r = 0; r < cfg.n_readers; ++r) {
        threads.emplace_back(
            detail::reader_thread<Queue, T>,
            std::ref(queue), std::ref(sync),
            cfg.first_cpu + cfg.n_writers + r, cfg.pin_threads,
            cfg.warmup_rounds, cfg.samples_per_role,
            overhead, std::ref(reader_buf[r]));
    }

    const unsigned total = cfg.n_writers + cfg.n_readers;
    while (sync.ready.load(std::memory_order_acquire) < total)
        std::this_thread::yield();

    sync.go.store(true, std::memory_order_release);

    for (auto& t : threads) t.join();

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

    Result res;
    res.timing_overhead  = overhead;
    res.n_writers        = cfg.n_writers;
    res.n_readers        = cfg.n_readers;
    res.samples_per_role = cfg.samples_per_role;
    res.enqueue          = compute_stats(all_enq);
    res.dequeue          = compute_stats(all_deq);
    res.enqueue_samples  = std::move(writer_buf);
    res.dequeue_samples  = std::move(reader_buf);

    res.write_csv(cfg.csv_path);

    return res;
}

} // namespace bench
