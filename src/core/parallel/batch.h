#pragma once
// Batch of independent thermal rod solves, run serially or on a worker pool.
//
// Both paths call the same fea::solve() on the same generated cases and write
// each result into a pre-allocated slot indexed by case number, so output
// order is deterministic and independent of scheduling.

#include "core/fea/thermal_rod.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace wb::parallel {

struct BatchConfig {
    std::uint64_t seed = 20260101;
    int caseCount = 500;
    int elementsPerCase = 10000;
    double kMin = 1.0, kMax = 400.0;     // W/(m K)
    double qMin = 1.0e3, qMax = 1.0e6;   // W/m^3
};

void validate(const BatchConfig& config);

// Reproducible case generator. Uses std::mt19937_64 (whose output sequence is
// fixed by the C++ standard) and maps raw 64-bit draws to [0,1) with an
// explicit 53-bit conversion, so the same seed gives bit-identical cases on
// every compiler and standard library.
std::vector<fea::RodProblem> generateBatch(const BatchConfig& config);

struct BatchResult {
    // Nodal temperatures of each case, in input order. Coordinates are not
    // stored: they follow from (L, N) and are identical for both paths.
    std::vector<std::vector<double>> temperatures;
};

// Progress callback: (completed cases, total cases). Called from worker threads.
using ProgressFn = std::function<void(std::size_t, std::size_t)>;

class Cancelled : public std::runtime_error {
public:
    Cancelled() : std::runtime_error("Operation cancelled by the user.") {}
};

// Throws Cancelled if `cancel` becomes true; partial results are discarded.
BatchResult runSerial(const std::vector<fea::RodProblem>& cases,
                      const std::atomic<bool>* cancel = nullptr,
                      const ProgressFn& progress = {});

class WorkerPool;

// The pool's threads pull fixed-size chunks of cases from its bounded queue.
BatchResult runParallel(const std::vector<fea::RodProblem>& cases, WorkerPool& pool,
                        const std::atomic<bool>* cancel = nullptr,
                        const ProgressFn& progress = {});

// Convenience overload: creates a pool of `workers` threads for this call.
BatchResult runParallel(const std::vector<fea::RodProblem>& cases, std::size_t workers,
                        const std::atomic<bool>* cancel = nullptr,
                        const ProgressFn& progress = {});

// Max |T_a - T_b| over every node of every case. Throws if shapes differ.
double maxAbsDifference(const BatchResult& a, const BatchResult& b);

// ---------------------------------------------------------------------------
// Benchmark: warm-up + repeated timed runs for serial and 1..maxWorkers.
// ---------------------------------------------------------------------------

struct BenchmarkConfig {
    BatchConfig batch;
    int maxWorkers = 4;      // capped at the number of logical CPUs
    int warmupRuns = 1;
    int measuredRuns = 5;
};

struct TimingRow {
    std::string mode;          // "serial" or "parallel"
    int workers = 1;
    std::vector<double> runSeconds;
    double medianSeconds = 0.0;
    double speedup = 0.0;      // T1 / Tp, T1 = median of 1-worker pool run
    double efficiency = 0.0;   // speedup / p
    double maxAbsDiffVsSerial = 0.0; // [C]
};

struct BenchmarkReport {
    BenchmarkConfig config;
    std::size_t logicalCpus = 1;
    std::vector<TimingRow> rows;
    double toleranceC = 1e-8;
    bool agreementPassed = false;
    std::string note; // e.g. single-CPU disclosure
};

// Stage callback: (human-readable stage, fraction complete in [0,1]).
using StageFn = std::function<void(const std::string&, double)>;

BenchmarkReport runBenchmark(const BenchmarkConfig& config,
                             const std::atomic<bool>* cancel = nullptr,
                             const StageFn& stage = {});

double median(std::vector<double> values);

} // namespace wb::parallel
