#include "core/parallel/batch.h"
#include "core/parallel/worker_pool.h"
#include "test_framework.h"

#include <atomic>
#include <chrono>
#include <random>
#include <thread>

using namespace wb::parallel;

TEST_CASE("pool: runs every submitted task exactly once") {
    std::atomic<int> counter{0};
    WorkerPool pool(4, 3);
    for (int i = 0; i < 1000; ++i) pool.submit([&] { counter.fetch_add(1); });
    pool.wait();
    CHECK(counter.load() == 1000);
}

TEST_CASE("pool: never exceeds its worker count") {
    std::atomic<int> running{0}, peak{0};
    WorkerPool pool(3, 2);
    for (int i = 0; i < 30; ++i) {
        pool.submit([&] {
            const int now = running.fetch_add(1) + 1;
            int expected = peak.load();
            while (now > expected && !peak.compare_exchange_weak(expected, now)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            running.fetch_sub(1);
        });
    }
    pool.wait();
    CHECK(peak.load() <= 3);
    CHECK(peak.load() >= 1);
}

TEST_CASE("pool: task exceptions surface in wait()") {
    WorkerPool pool(2, 2);
    pool.submit([] { throw std::runtime_error("boom"); });
    CHECK_THROWS_WITH(pool.wait(), std::runtime_error, "boom");
    // The pool stays usable afterwards.
    std::atomic<int> ok{0};
    pool.submit([&] { ok = 1; });
    pool.wait();
    CHECK(ok.load() == 1);
}

TEST_CASE("batch: generator is reproducible for a fixed seed") {
    BatchConfig c;
    c.caseCount = 50;
    c.elementsPerCase = 10;
    const auto a = generateBatch(c);
    const auto b = generateBatch(c);
    CHECK(a.size() == 50);
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].conductivity == b[i].conductivity);
        CHECK(a[i].heatGeneration == b[i].heatGeneration);
        CHECK(a[i].conductivity >= c.kMin && a[i].conductivity <= c.kMax);
        CHECK(a[i].heatGeneration >= c.qMin && a[i].heatGeneration <= c.qMax);
    }
    c.seed += 1;
    CHECK(generateBatch(c)[0].conductivity != a[0].conductivity);
}

TEST_CASE("batch: generator output is pinned across platforms") {
    // mt19937_64 is fully specified by the standard and the [0,1) mapping is
    // explicit, so this value must be identical with GCC, Clang and MSVC.
    BatchConfig c;
    c.seed = 42;
    c.caseCount = 1;
    c.elementsPerCase = 1;
    const auto cases = generateBatch(c);
    std::mt19937_64 rng(42);
    const double expectedK = 1.0 + 399.0 * (static_cast<double>(rng() >> 11) * 0x1.0p-53);
    CHECK(cases[0].conductivity == expectedK);
}

TEST_CASE("batch: serial and parallel results agree in input order") {
    BatchConfig c;
    c.caseCount = 37; // not a multiple of the chunk size
    c.elementsPerCase = 500;
    const auto cases = generateBatch(c);
    const BatchResult serial = runSerial(cases);
    for (std::size_t workers : {1u, 2u, 3u, 4u}) {
        const BatchResult par = runParallel(cases, workers);
        CHECK(par.temperatures.size() == cases.size());
        CHECK(maxAbsDifference(serial, par) <= 1e-8);
    }
    // order check: each case's peak temperature must match its own parameters
    for (std::size_t i = 0; i < cases.size(); ++i) {
        CHECK_NEAR(serial.temperatures[i][250], wb::fea::exactTemperature(cases[i], 0.5), 1e-6);
    }
}

TEST_CASE("batch: cancellation stops both paths and reports Cancelled") {
    BatchConfig c;
    c.caseCount = 200;
    c.elementsPerCase = 2000;
    const auto cases = generateBatch(c);
    std::atomic<bool> cancel{true};
    CHECK_THROWS_AS(runSerial(cases, &cancel), Cancelled);
    CHECK_THROWS_AS(runParallel(cases, 3, &cancel), Cancelled);
}

TEST_CASE("batch: invalid configurations are rejected") {
    BatchConfig c;
    c.caseCount = 0;
    CHECK_THROWS_WITH(generateBatch(c), std::invalid_argument, "Case count");
    c = BatchConfig{};
    c.kMin = -1.0;
    CHECK_THROWS_WITH(generateBatch(c), std::invalid_argument, "Conductivity");
    c = BatchConfig{};
    c.caseCount = 100000;
    c.elementsPerCase = 100000;
    CHECK_THROWS_WITH(generateBatch(c), std::invalid_argument, "memory");
}

TEST_CASE("benchmark: small run produces consistent rows and passes agreement") {
    BenchmarkConfig cfg;
    cfg.batch.caseCount = 16;
    cfg.batch.elementsPerCase = 200;
    cfg.maxWorkers = 2;
    cfg.warmupRuns = 1;
    cfg.measuredRuns = 3;
    const BenchmarkReport r = runBenchmark(cfg);
    CHECK(r.agreementPassed);
    CHECK(r.rows.front().mode == "serial");
    CHECK(r.rows.size() >= 2);
    for (const auto& row : r.rows) {
        CHECK(row.runSeconds.size() == 3);
        CHECK(row.medianSeconds > 0.0);
    }
    CHECK_NEAR(r.rows[1].speedup, 1.0, 1e-12); // 1-worker row is the T1 reference
    CHECK_NEAR(median({3.0, 1.0, 2.0}), 2.0, 0.0);
    CHECK_NEAR(median({4.0, 1.0, 2.0, 3.0}), 2.5, 0.0);
}
