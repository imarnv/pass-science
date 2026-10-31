#include "core/parallel/batch.h"

#include "core/parallel/worker_pool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <random>

namespace wb::parallel {

namespace {

constexpr int kMaxCases = 100'000;
constexpr long long kMaxTotalNodes = 50'000'000; // bounds memory of one result set (~400 MB)
constexpr std::size_t kChunkSize = 4;             // cases per queued task

// Uniform double in [0, 1) from the top 53 bits of a 64-bit draw.
double unitInterval(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * 0x1.0p-53;
}

double uniform(std::mt19937_64& rng, double lo, double hi) {
    return lo + (hi - lo) * unitInterval(rng);
}

void checkCancelled(const std::atomic<bool>* cancel) {
    if (cancel && cancel->load(std::memory_order_relaxed)) throw Cancelled();
}

std::vector<double> solveTemperatures(const fea::RodProblem& problem) {
    return fea::solve(problem).T;
}

} // namespace

void validate(const BatchConfig& c) {
    if (c.caseCount < 1 || c.caseCount > kMaxCases) {
        throw std::invalid_argument("Case count must be between 1 and " + std::to_string(kMaxCases) + ".");
    }
    if (c.elementsPerCase < 1 || c.elementsPerCase > fea::kMaxElements) {
        throw std::invalid_argument("Elements per case must be between 1 and " +
                                    std::to_string(fea::kMaxElements) + ".");
    }
    const long long totalNodes = static_cast<long long>(c.caseCount) * (c.elementsPerCase + 1);
    if (totalNodes > kMaxTotalNodes) {
        throw std::invalid_argument("Case count x (elements + 1) must not exceed " +
                                    std::to_string(kMaxTotalNodes) +
                                    " to keep memory bounded. Reduce cases or mesh size.");
    }
    if (!(c.kMin > 0.0) || !(c.kMax >= c.kMin) || !std::isfinite(c.kMax)) {
        throw std::invalid_argument("Conductivity range must satisfy 0 < k_min <= k_max.");
    }
    if (!(c.qMin > 0.0) || !(c.qMax >= c.qMin) || !std::isfinite(c.qMax)) {
        throw std::invalid_argument("Heat generation range must satisfy 0 < q_min <= q_max.");
    }
}

std::vector<fea::RodProblem> generateBatch(const BatchConfig& config) {
    validate(config);
    std::mt19937_64 rng(config.seed);
    std::vector<fea::RodProblem> cases(static_cast<std::size_t>(config.caseCount));
    for (fea::RodProblem& p : cases) {
        p.elements = config.elementsPerCase;
        p.conductivity = uniform(rng, config.kMin, config.kMax);
        p.heatGeneration = uniform(rng, config.qMin, config.qMax);
        // Geometry and boundary temperatures stay at the reference values
        // (L = 1 m, A = 1e-4 m^2, T(0) = 100 C, T(L) = 20 C).
    }
    return cases;
}

BatchResult runSerial(const std::vector<fea::RodProblem>& cases, const std::atomic<bool>* cancel,
                      const ProgressFn& progress) {
    BatchResult result;
    result.temperatures.resize(cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        checkCancelled(cancel);
        result.temperatures[i] = solveTemperatures(cases[i]);
        if (progress) progress(i + 1, cases.size());
    }
    return result;
}

BatchResult runParallel(const std::vector<fea::RodProblem>& cases, WorkerPool& pool,
                        const std::atomic<bool>* cancel, const ProgressFn& progress) {
    BatchResult result;
    result.temperatures.resize(cases.size());
    std::atomic<std::size_t> completed{0};

    for (std::size_t begin = 0; begin < cases.size(); begin += kChunkSize) {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;
        const std::size_t end = std::min(begin + kChunkSize, cases.size());
        pool.submit([&, begin, end] {
            for (std::size_t i = begin; i < end; ++i) {
                checkCancelled(cancel);
                // Each task writes only its own slots: no locking needed.
                result.temperatures[i] = solveTemperatures(cases[i]);
            }
            const std::size_t done = completed.fetch_add(end - begin) + (end - begin);
            if (progress) progress(done, cases.size());
        });
    }
    pool.wait(); // rethrows Cancelled (or any solver error) from workers
    checkCancelled(cancel);
    return result;
}

BatchResult runParallel(const std::vector<fea::RodProblem>& cases, std::size_t workers,
                        const std::atomic<bool>* cancel, const ProgressFn& progress) {
    // Queue capacity of 2 chunks per worker keeps the producer only slightly
    // ahead of the consumers; submit() blocks when the queue is full.
    const std::size_t n = std::max<std::size_t>(1, workers);
    WorkerPool pool(n, 2 * n);
    return runParallel(cases, pool, cancel, progress);
}

double maxAbsDifference(const BatchResult& a, const BatchResult& b) {
    if (a.temperatures.size() != b.temperatures.size()) {
        throw std::invalid_argument("Batch results have different case counts.");
    }
    double maxDiff = 0.0;
    for (std::size_t i = 0; i < a.temperatures.size(); ++i) {
        const auto& ta = a.temperatures[i];
        const auto& tb = b.temperatures[i];
        if (ta.size() != tb.size()) throw std::invalid_argument("Batch results have different mesh sizes.");
        for (std::size_t j = 0; j < ta.size(); ++j) maxDiff = std::max(maxDiff, std::abs(ta[j] - tb[j]));
    }
    return maxDiff;
}

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t mid = values.size() / 2;
    return values.size() % 2 ? values[mid] : 0.5 * (values[mid - 1] + values[mid]);
}

BenchmarkReport runBenchmark(const BenchmarkConfig& config, const std::atomic<bool>* cancel,
                             const StageFn& stage) {
    if (config.measuredRuns < 1 || config.measuredRuns > 50) {
        throw std::invalid_argument("Measured repetitions must be between 1 and 50.");
    }
    if (config.warmupRuns < 0 || config.warmupRuns > 10) {
        throw std::invalid_argument("Warm-up runs must be between 0 and 10.");
    }
    if (config.maxWorkers < 1 || config.maxWorkers > 64) {
        throw std::invalid_argument("Maximum workers must be between 1 and 64.");
    }

    BenchmarkReport report;
    report.config = config;
    report.logicalCpus = logicalCpuCount();
    const int maxWorkers = static_cast<int>(
        std::min<std::size_t>(static_cast<std::size_t>(config.maxWorkers), report.logicalCpus));
    if (report.logicalCpus == 1) {
        report.note = "Only one logical CPU is available: the parallel path is exercised and verified, "
                      "but speedup cannot be assessed on this machine.";
    } else if (maxWorkers < config.maxWorkers) {
        report.note = "Worker count capped at " + std::to_string(maxWorkers) + " logical CPUs.";
    }

    // Case generation is excluded from all timings.
    const std::vector<fea::RodProblem> cases = generateBatch(config.batch);

    struct Mode { std::string name; int workers; };
    std::vector<Mode> modes{{"serial", 1}};
    for (int p = 1; p <= maxWorkers; ++p) modes.push_back({"parallel", p});

    const int runsPerMode = config.warmupRuns + config.measuredRuns;
    const double totalRuns = static_cast<double>(modes.size() * static_cast<std::size_t>(runsPerMode));
    int finishedRuns = 0;

    BatchResult serialReference;
    for (const Mode& mode : modes) {
        // One pool per worker count, created before timing and reused for the
        // warm-up and measured runs: thread start-up is excluded from Tp.
        std::unique_ptr<WorkerPool> pool;
        if (mode.name == "parallel") {
            const auto n = static_cast<std::size_t>(mode.workers);
            pool = std::make_unique<WorkerPool>(n, 2 * n);
        }
        TimingRow row;
        row.mode = mode.name;
        row.workers = mode.workers;
        for (int run = 0; run < runsPerMode; ++run) {
            checkCancelled(cancel);
            const bool warmup = run < config.warmupRuns;
            if (stage) {
                const std::string label = mode.name == "serial" ? std::string("Serial")
                                                                : std::to_string(mode.workers) + " worker" +
                                                                      (mode.workers > 1 ? "s" : "");
                stage(label + (warmup ? " - warm-up" : " - run " + std::to_string(run - config.warmupRuns + 1)),
                      finishedRuns / totalRuns);
            }

            const auto start = std::chrono::steady_clock::now();
            BatchResult result = mode.name == "serial"
                                     ? runSerial(cases, cancel)
                                     : runParallel(cases, *pool, cancel);
            const auto stop = std::chrono::steady_clock::now();

            if (!warmup) row.runSeconds.push_back(std::chrono::duration<double>(stop - start).count());
            if (mode.name == "serial" && run == 0) serialReference = std::move(result);
            else if (mode.name == "parallel" && run == runsPerMode - 1)
                row.maxAbsDiffVsSerial = maxAbsDifference(serialReference, result);
            ++finishedRuns;
        }
        row.medianSeconds = median(row.runSeconds);
        report.rows.push_back(std::move(row));
    }

    // Speedup is relative to the single-worker pool run (same code path as Tp).
    const double t1 = report.rows.size() > 1 ? report.rows[1].medianSeconds : report.rows[0].medianSeconds;
    report.agreementPassed = true;
    for (TimingRow& row : report.rows) {
        row.speedup = row.medianSeconds > 0 ? t1 / row.medianSeconds : 0.0;
        row.efficiency = row.speedup / row.workers;
        if (row.maxAbsDiffVsSerial > report.toleranceC) report.agreementPassed = false;
    }
    if (stage) stage("Complete", 1.0);
    return report;
}

} // namespace wb::parallel
