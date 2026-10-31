#pragma once
// Hand-derived backpropagation for a one-hidden-unit regression network.
//
//   z     = w x + b
//   h     = tanh(z)
//   y_hat = v h + c
//   J     = 0.5 (y_hat - y)^2
//
// Chain rule (no automatic differentiation):
//   dJ/dy_hat = y_hat - y                       (= delta)
//   dJ/dv     = delta * h
//   dJ/dc     = delta
//   dJ/dh     = delta * v
//   dh/dz     = 1 - tanh(z)^2 = 1 - h^2
//   dJ/dz     = delta * v * (1 - h^2)
//   dJ/dw     = dJ/dz * x
//   dJ/db     = dJ/dz

#include <vector>

namespace wb::ml {

struct Params {
    double w = 0.5, b = 0.0, v = 1.0, c = 0.0;
};

struct Sample {
    double x = 1.0, y = 2.0;
};

struct Forward {
    double z = 0.0, h = 0.0, yHat = 0.0, loss = 0.0;
};

struct Gradients {
    double dw = 0.0, db = 0.0, dv = 0.0, dc = 0.0;
};

Forward forward(const Params& p, const Sample& s);
Gradients backward(const Params& p, const Sample& s);

// Central finite differences: (J(theta + eps) - J(theta - eps)) / (2 eps).
Gradients numericalGradients(const Params& p, const Sample& s, double epsilon);

// Simultaneous update: every parameter uses gradients from the *same* (old) parameters.
Params gradientStep(const Params& p, const Gradients& g, double learningRate);

struct TrainingTrace {
    std::vector<double> loss;     // loss[k] = J before step k+1 (loss[0] is the initial loss)
    std::vector<Params> params;   // params[k] matches loss[k]
};

// Records the initial state, then `steps` gradient-descent updates.
TrainingTrace train(Params start, const Sample& s, double learningRate, int steps);

} // namespace wb::ml
