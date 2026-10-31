#include "core/ml/backprop.h"

#include <cmath>
#include <stdexcept>

namespace wb::ml {

Forward forward(const Params& p, const Sample& s) {
    Forward f;
    f.z = p.w * s.x + p.b;
    f.h = std::tanh(f.z);
    f.yHat = p.v * f.h + p.c;
    const double residual = f.yHat - s.y;
    f.loss = 0.5 * residual * residual;
    return f;
}

Gradients backward(const Params& p, const Sample& s) {
    const Forward f = forward(p, s);
    const double delta = f.yHat - s.y;        // dJ/dy_hat
    const double dJdz = delta * p.v * (1.0 - f.h * f.h);

    Gradients g;
    g.dv = delta * f.h;
    g.dc = delta;
    g.dw = dJdz * s.x;
    g.db = dJdz;
    return g;
}

Gradients numericalGradients(const Params& p, const Sample& s, double epsilon) {
    if (!(epsilon > 0.0)) throw std::invalid_argument("Finite-difference epsilon must be positive.");
    auto central = [&](double Params::*member) {
        Params plus = p, minus = p;
        plus.*member += epsilon;
        minus.*member -= epsilon;
        return (forward(plus, s).loss - forward(minus, s).loss) / (2.0 * epsilon);
    };
    Gradients g;
    g.dw = central(&Params::w);
    g.db = central(&Params::b);
    g.dv = central(&Params::v);
    g.dc = central(&Params::c);
    return g;
}

Params gradientStep(const Params& p, const Gradients& g, double learningRate) {
    Params next;
    next.w = p.w - learningRate * g.dw;
    next.b = p.b - learningRate * g.db;
    next.v = p.v - learningRate * g.dv;
    next.c = p.c - learningRate * g.dc;
    return next;
}

TrainingTrace train(Params start, const Sample& s, double learningRate, int steps) {
    if (steps < 0) throw std::invalid_argument("Number of training steps must be non-negative.");
    TrainingTrace trace;
    trace.loss.reserve(static_cast<std::size_t>(steps) + 1);
    trace.params.reserve(static_cast<std::size_t>(steps) + 1);
    Params p = start;
    for (int k = 0; k <= steps; ++k) {
        trace.params.push_back(p);
        trace.loss.push_back(forward(p, s).loss);
        if (k < steps) p = gradientStep(p, backward(p, s), learningRate);
    }
    return trace;
}

} // namespace wb::ml
