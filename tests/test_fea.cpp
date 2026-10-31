#include "core/fea/thermal_rod.h"
#include "test_framework.h"

#include <cmath>
#include <limits>
#include <stdexcept>

using namespace wb::fea;

namespace {
RodProblem reference(int n) {
    RodProblem p; // defaults are the assignment's reference problem
    p.elements = n;
    return p;
}
} // namespace

TEST_CASE("fea: reference defaults match the assignment brief") {
    const RodProblem p;
    CHECK(p.length == 1.0);
    CHECK(p.area == 1e-4);
    CHECK(p.conductivity == 10.0);
    CHECK(p.heatGeneration == 1e4);
    CHECK(p.tempLeft == 100.0);
    CHECK(p.tempRight == 20.0);
}

TEST_CASE("fea: exact solution satisfies the boundary conditions and peak") {
    const RodProblem p = reference(10);
    CHECK_NEAR(exactTemperature(p, 0.0), 100.0, 1e-12);
    CHECK_NEAR(exactTemperature(p, 1.0), 20.0, 1e-12);
    // T = 100 - 80x + 500x(1-x): dT/dx = 0 at x = 0.42, T = 188.2
    CHECK_NEAR(exactTemperature(p, 0.42), 188.2, 1e-10);
}

TEST_CASE("fea: boundary temperatures are imposed exactly") {
    for (int n : {1, 2, 10, 80}) {
        const RodSolution s = solve(reference(n));
        CHECK(s.T.size() == static_cast<std::size_t>(n) + 1);
        CHECK(s.T.front() == 100.0);
        CHECK(s.T.back() == 20.0);
        CHECK_NEAR(s.x.back(), 1.0, 1e-15);
    }
}

TEST_CASE("fea: two-element mesh matches a hand calculation") {
    // h = 0.5, kA/h = 2e-3 W/K, interior load = 2 * qAh/2 = 0.5 W
    // 4e-3 T1 - 2e-3 (100 + 20) = 0.5  ->  T1 = 185
    const RodSolution s = solve(reference(2));
    CHECK_NEAR(s.T[1], 185.0, 1e-10);
}

TEST_CASE("fea: linear elements are nodally exact for this problem") {
    // For 1-D linear elements with a consistent load vector, the Green's
    // function lies in the FE space, so nodal values equal the exact solution.
    for (int n : {10, 20, 40, 80}) {
        const RodProblem p = reference(n);
        const ErrorReport e = evaluateError(p, solve(p));
        CHECK(e.maxNodalError < 1e-9);
    }
}

TEST_CASE("fea: midpoint error equals q h^2 / (8k) and converges at order 2") {
    double previousError = 0.0;
    ErrorReport previous;
    for (int n : {10, 20, 40, 80}) {
        const RodProblem p = reference(n);
        const ErrorReport e = evaluateError(p, solve(p));
        const double h = 1.0 / n;
        CHECK_NEAR(e.maxMidpointError, p.heatGeneration * h * h / (8.0 * p.conductivity), 1e-9);
        if (previousError > 0.0) CHECK_NEAR(observedOrder(previous, e), 2.0, 1e-6);
        previousError = e.maxMidpointError;
        previous = e;
    }
    CHECK_NEAR(previousError, 0.01953125, 1e-9); // N = 80
}

TEST_CASE("fea: zero heat generation gives the exact linear profile everywhere") {
    RodProblem p = reference(7);
    p.heatGeneration = 0.0;
    const RodSolution s = solve(p);
    const ErrorReport e = evaluateError(p, s);
    CHECK(e.maxMidpointError < 1e-10);
    CHECK_NEAR(interpolate(s, 0.5), 60.0, 1e-10);
}

TEST_CASE("fea: interpolation reproduces nodes and clamps outside the rod") {
    const RodSolution s = solve(reference(4));
    CHECK_NEAR(interpolate(s, 0.25), s.T[1], 1e-12);
    CHECK_NEAR(interpolate(s, -1.0), 100.0, 1e-12);
    CHECK_NEAR(interpolate(s, 2.0), 20.0, 1e-12);
    CHECK_NEAR(interpolate(s, 0.125), 0.5 * (s.T[0] + s.T[1]), 1e-12);
}

TEST_CASE("fea: large mesh stays accurate (no round-off blow-up)") {
    const RodProblem p = reference(200000);
    const ErrorReport e = evaluateError(p, solve(p));
    CHECK(e.maxNodalError < 1e-6);
}

TEST_CASE("fea: invalid parameters are rejected with clear messages") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    RodProblem p;
    p.elements = 0;
    CHECK_THROWS_WITH(solve(p), std::invalid_argument, "at least 1");
    p = RodProblem{};
    p.elements = kMaxElements + 1;
    CHECK_THROWS_WITH(solve(p), std::invalid_argument, "must not exceed");
    p = RodProblem{};
    p.conductivity = 0.0;
    CHECK_THROWS_WITH(solve(p), std::invalid_argument, "conductivity");
    p = RodProblem{};
    p.length = -1.0;
    CHECK_THROWS_WITH(solve(p), std::invalid_argument, "length");
    p = RodProblem{};
    p.area = nan;
    CHECK_THROWS_WITH(solve(p), std::invalid_argument, "finite");
    p = RodProblem{};
    p.heatGeneration = inf;
    CHECK_THROWS_WITH(solve(p), std::invalid_argument, "finite");
    p = RodProblem{};
    p.tempLeft = -300.0;
    CHECK_THROWS_WITH(solve(p), std::invalid_argument, "absolute zero");
}
