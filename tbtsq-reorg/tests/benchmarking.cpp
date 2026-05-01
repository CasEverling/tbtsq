/**
 * benchmarking.cpp
 *
 * Runs every queue implementation through a matrix of workloads and writes
 * one CSV per (queue × workload) pair.  The Python plotting script reads
 * those CSVs to produce tail-latency figures.
 *
 * Workloads
 * ─────────
 *  lightly_loaded   1 writer  / 1 reader  / 100 000 samples
 *  balanced         2 writers / 2 readers / 200 000 samples
 *  write_heavy      4 writers / 1 reader  / 200 000 samples
 *  read_heavy       1 writer  / 4 readers / 200 000 samples
 *  contended        4 writers / 4 readers / 400 000 samples
 *
 * Build
 * ─────
 *  # From the tests/ directory:
 *  g++ -O2 -std=c++20 \
 *      benchmarking.cpp \
 *      ../coarse_mutex.cpp ../two_locks.cpp ../ms.cpp \
 *      ../hazard_pointer.cpp ../vuykov.cpp \
 *      -I.. -lpthread -o bench_all && ./bench_all
 *
 * Output
 * ──────
 *  results/<queue>_<workload>.csv   (role,thread_id,seq,cycles)
 *  results/summary.csv              (queue,workload,role,p50,p99,p999,p9999,max,mean,stddev)
 */

#include "queue_benchmark.hpp"

#include "../coarse_mutex.hpp"
#include "../two_locks.hpp"
#include "../ms.hpp"
#include "../hazard_pointer.hpp"
#include "../spsc_ring.hpp"
#include "../vuykov.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ── Workload descriptor ───────────────────────────────────────────────────────

struct Workload {
    std::string name;
    unsigned    n_writers;
    unsigned    n_readers;
    unsigned    samples_per_role;
};

static const std::vector<Workload> WORKLOADS = {
    { "lightly_loaded",  1, 1, 100'000 },
    { "balanced",        2, 2, 200'000 },
    { "write_heavy",     4, 1, 200'000 },
    { "read_heavy",      1, 4, 200'000 },
    { "contended",       4, 4, 400'000 },
};

// ── Queue factory helpers ─────────────────────────────────────────────────────
//
// Each queue has different construction requirements (some need a capacity,
// the hazard-pointer queue uses a two-step init), so we wrap them in lambdas
// that return a std::function<bench::Result()>.  This lets the harness loop
// stay generic without templating on queue type.

using RunFn = std::function<bench::Result(const bench::Config&)>;

// Minimum ring size for ring-buffer queues: must be strictly larger than the
// maximum number of in-flight items.  We pick a generous power-of-two.
static constexpr size_t RING_CAPACITY = 1 << 18; // 262 144

struct QueueEntry {
    std::string name;
    // Returns a RunFn for one workload.  We recreate the queue fresh for each
    // workload so state never carries over between runs.
    std::function<RunFn()> make_runner;
};

static std::vector<QueueEntry> build_queue_entries() {
    return {
        {
            "coarse_mutex",
            []() -> RunFn {
                auto q = std::make_shared<CoarseQueue<int>>();
                return [q](const bench::Config& cfg) {
                    return bench::run<int>(*q, cfg, 42);
                };
            }
        },
        {
            "two_locks",
            []() -> RunFn {
                auto q = std::make_shared<TwoLocksQueue<int>>();
                return [q](const bench::Config& cfg) {
                    return bench::run<int>(*q, cfg, 42);
                };
            }
        },
        {
            "ms_queue",
            []() -> RunFn {
                auto q = std::make_shared<MSQueue<int>>();
                return [q](const bench::Config& cfg) {
                    return bench::run<int>(*q, cfg, 42);
                };
            }
        },
        {
            "spsc_ring",
            // SPSC: only valid for the lightly_loaded (1w/1r) workload.
            // For multi-producer/consumer workloads we skip it (returns empty Result).
            []() -> RunFn {
                auto q = std::make_shared<SpscRing<int>>(RING_CAPACITY);
                return [q](const bench::Config& cfg) -> bench::Result {
                    if (cfg.n_writers != 1 || cfg.n_readers != 1) {
                        std::cout << "    [spsc_ring] skipped (SPSC only)\n";
                        return {};
                    }
                    return bench::run<int>(*q, cfg, 42);
                };
            }
        },
        {
            "vuykov",
            []() -> RunFn {
                auto q = std::make_shared<VuykovQueue<int>>(RING_CAPACITY);
                return [q](const bench::Config& cfg) {
                    return bench::run<int>(*q, cfg, 42);
                };
            }
        },
        {
            "hazard_pointer",
            []() -> RunFn {
                // MSHPQ uses a two-phase init: create() then instantiate() per thread.
                // The bench::run() API expects a single queue object implementing
                // enqueue/dequeue, so we use the Instance handle directly.
                auto factory = MSHPQ<int>::create(/*max_users=*/16);
                auto inst    = factory->instantiate();
                return [inst](const bench::Config& cfg) {
                    return bench::run<int>(*inst, cfg, 42);
                };
            }
        },
    };
}

// ── Summary CSV helpers ───────────────────────────────────────────────────────

struct SummaryRow {
    std::string queue;
    std::string workload;
    std::string role;       // "enqueue" or "dequeue"
    uint64_t    p50, p99, p999, p9999, max;
    double      mean, stddev;
};

static void write_summary(const std::string& path,
                           const std::vector<SummaryRow>& rows)
{
    std::ofstream f(path);
    f << "queue,workload,role,p50,p99,p99.9,p99.99,max,mean,stddev\n";
    for (const auto& r : rows) {
        f << r.queue    << ','
          << r.workload << ','
          << r.role     << ','
          << r.p50      << ','
          << r.p99      << ','
          << r.p999     << ','
          << r.p9999    << ','
          << r.max      << ','
          << std::fixed << std::setprecision(2)
          << r.mean     << ','
          << r.stddev   << '\n';
    }
    std::cout << "[summary] written → " << path << '\n';
}

static SummaryRow make_row(const std::string& q, const std::string& wl,
                            const std::string& role, const bench::Stats& s)
{
    return { q, wl, role, s.p50, s.p99, s.p999, s.p9999,
             s.max_cycles, s.mean, s.stddev };
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    fs::create_directories("results");

    std::vector<SummaryRow> summary;

    for (const auto& workload : WORKLOADS) {
        std::cout << "\n══════════════════════════════════════════\n";
        std::cout << "  Workload : " << workload.name << '\n';
        std::cout << "  Writers  : " << workload.n_writers
                  << "  Readers : " << workload.n_readers
                  << "  Samples/thread : " << workload.samples_per_role << '\n';
        std::cout << "══════════════════════════════════════════\n";

        auto entries = build_queue_entries();   // fresh queue per workload

        for (auto& entry : entries) {
            std::cout << "\n  ── " << entry.name << " ──\n";

            auto runner = entry.make_runner();

            const std::string csv = "results/" + entry.name
                                  + "_" + workload.name + ".csv";

            bench::Config cfg;
            cfg.n_writers        = workload.n_writers;
            cfg.n_readers        = workload.n_readers;
            cfg.samples_per_role = workload.samples_per_role;
            cfg.warmup_rounds    = 5;
            cfg.pin_threads      = false;
            cfg.first_cpu        = 0;
            cfg.csv_path         = csv;

            bench::Result res = runner(cfg);

            // Skip skipped runs (empty result from SPSC on multi-thread workloads)
            if (res.samples_per_role == 0) continue;

            res.print();

            summary.push_back(make_row(entry.name, workload.name, "enqueue", res.enqueue));
            summary.push_back(make_row(entry.name, workload.name, "dequeue", res.dequeue));
        }
    }

    write_summary("results/summary.csv", summary);

    std::cout << "\n[done] all results in ./results/\n";
    return 0;
}
