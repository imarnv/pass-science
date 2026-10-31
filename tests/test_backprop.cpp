#include "core/ml/backprop.h"
#include "test_framework.h"

#include <cmath>

using namespace wb::ml;

TEST_CASE("backprop: forward pass matches hand-computed values") {
    const Forward f = forward(Params{}, Sample{});
    CHECK_NEAR(f.z, 0.5, 1e-15);
    CHECK_NEAR(f.h, std::tanh(0.5), 1e-15);
    CHECK_NEAR(f.h, 0.46211715726, 1e-10);
    CHECK_NEAR(f.yHat, 0.46211715726, 1e-10);
    CHECK_NEAR(f.loss, 1.18254181900, 1e-10);
}

TEST_CASE("backprop: analytic gradients match the hand derivation") {
    const Gradients g = backward(Params{}, Sample{});
    const double h = std::tanh(0.5);
    const double delta = h - 2.0;
    CHECK_NEAR(g.dc, delta, 1e-15);
    CHECK_NEAR(g.dv, delta * h, 1e-15);
    CHECK_NEAR(g.dw, delta * (1.0 - h * h), 1e-15);
    CHECK_NEAR(g.db, g.dw, 1e-15); // x = 1
    CHECK_NEAR(g.dw, -1.20946447524, 1e-10);
    CHECK_NEAR(g.dv, -0.71068204749, 1e-10);
}

TEST_CASE("backprop: central finite differences agree within 1e-5") {
    const Gradients a = backward(Params{}, Sample{});
    const Gradients n = numericalGradients(Params{}, Sample{}, 1e-6);
    CHECK_NEAR(a.dw, n.dw, 1e-5);
    CHECK_NEAR(a.db, n.db, 1e-5);
    CHECK_NEAR(a.dv, n.dv, 1e-5);
    CHECK_NEAR(a.dc, n.dc, 1e-5);
}

TEST_CASE("backprop: one simultaneous update uses original-parameter gradients") {
    const Params p0;
    const Gradients g = backward(p0, Sample{});
    const Params p1 = gradientStep(p0, g, 0.1);
    CHECK_NEAR(p1.w, 0.5 - 0.1 * g.dw, 1e-15);
    CHECK_NEAR(p1.b, 0.0 - 0.1 * g.db, 1e-15);
    CHECK_NEAR(p1.v, 1.0 - 0.1 * g.dv, 1e-15);
    CHECK_NEAR(p1.c, 0.0 - 0.1 * g.dc, 1e-15);
    // all gradients are negative (prediction too small), so all parameters increase
    CHECK(p1.w > p0.w && p1.b > p0.b && p1.v > p0.v && p1.c > p0.c);
    CHECK(forward(p1, Sample{}).loss < forward(p0, Sample{}).loss);
}

TEST_CASE("backprop: training reduces loss monotonically towards zero") {
    const TrainingTrace t = train(Params{}, Sample{}, 0.1, 100);
    CHECK(t.loss.size() == 101);
    for (std::size_t k = 1; k < t.loss.size(); ++k) CHECK(t.loss[k] <= t.loss[k - 1]);
    CHECK(t.loss.back() < 1e-3);
    CHECK_THROWS_AS(numericalGradients(Params{}, Sample{}, 0.0), std::invalid_argument);
}
