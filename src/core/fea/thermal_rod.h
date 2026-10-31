#pragma once
// Steady-state 1-D thermal finite-element model of a uniform rod.
//
// Governing equation (insulated lateral surface, constant properties):
//     -d/dx( k A dT/dx ) = q A          on 0 < x < L
//     T(0) = T_left,  T(L) = T_right     (Dirichlet at both ends)
//
// Discretisation: N uniform two-node linear (Lagrange) elements.
//   element stiffness  K_e = (k A / h) * [ 1 -1 ; -1 1 ]        [W/K]
//   element load       f_e = (q A h / 2) * [ 1 ; 1 ]             [W]
//
// Units used throughout (SI): x, L, h in m; A in m^2; k in W/(m K);
// q in W/m^3; temperatures in degrees C (only differences enter K).

#include <cstddef>
#include <vector>

namespace wb::fea {

struct RodProblem {
    double length = 1.0;           // L  [m]
    double area = 1.0e-4;          // A  [m^2]
    double conductivity = 10.0;    // k  [W/(m K)]
    double heatGeneration = 1.0e4; // q  [W/m^3]
    double tempLeft = 100.0;       // T(0) [C]
    double tempRight = 20.0;       // T(L) [C]
    int elements = 10;             // N  [-]
};

// Upper bound on N accepted by validate(); keeps a single request bounded in
// memory and time (one solve with 2e6 elements takes well under a second).
constexpr int kMaxElements = 2'000'000;

// Throws std::invalid_argument with a user-facing message if any parameter is
// non-finite, non-positive where positivity is required, or out of range.
void validate(const RodProblem& problem);

struct RodSolution {
    std::vector<double> x; // nodal coordinates [m], size N+1
    std::vector<double> T; // nodal temperatures [C], size N+1
};

// Assembles the global system, imposes both Dirichlet conditions and solves the
// resulting tridiagonal system with the Thomas algorithm (O(N)).
RodSolution solve(const RodProblem& problem);

// Closed-form solution: T(x) = T_left + (T_right - T_left) x / L + q/(2k) x (L - x).
// For the reference problem this is 100 - 80x/L + [q/(2k)] x (L - x).
double exactTemperature(const RodProblem& problem, double x);

// Evaluates the finite-element field at x using the linear shape functions of
// the element containing x. x outside [0, L] is clamped to the rod ends.
double interpolate(const RodSolution& solution, double x);

struct ErrorReport {
    double elementSize = 0.0;      // h [m]
    double maxNodalError = 0.0;    // max |T_h - T_exact| over nodes [C]
    double maxMidpointError = 0.0; // max |T_h - T_exact| over element midpoints [C]
};

ErrorReport evaluateError(const RodProblem& problem, const RodSolution& solution);

// Observed order of convergence between two meshes: log(e1/e2) / log(h1/h2).
double observedOrder(const ErrorReport& coarse, const ErrorReport& fine);

} // namespace wb::fea
