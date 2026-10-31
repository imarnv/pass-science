#include "core/fea/thermal_rod.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace wb::fea {

namespace {

void requireFinite(double value, const char* name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be a finite number.");
    }
}

void requirePositive(double value, const char* name) {
    requireFinite(value, name);
    if (value <= 0.0) {
        throw std::invalid_argument(std::string(name) + " must be greater than zero.");
    }
}

// Global tridiagonal matrix stored as three diagonals. For two-node elements
// each node couples only to its neighbours, so this is the exact sparsity.
struct Tridiagonal {
    std::vector<double> lower; // lower[i] = K(i, i-1), lower[0] unused
    std::vector<double> diag;  // diag[i]  = K(i, i)
    std::vector<double> upper; // upper[i] = K(i, i+1), upper[n-1] unused

    explicit Tridiagonal(std::size_t n) : lower(n, 0.0), diag(n, 0.0), upper(n, 0.0) {}
};

// Thomas algorithm. The reduced FE matrix is symmetric positive definite and
// diagonally dominant, so no pivoting is needed. Overwrites its inputs.
std::vector<double> solveTridiagonal(Tridiagonal& m, std::vector<double>& rhs) {
    const std::size_t n = rhs.size();
    for (std::size_t i = 1; i < n; ++i) {
        const double factor = m.lower[i] / m.diag[i - 1];
        m.diag[i] -= factor * m.upper[i - 1];
        rhs[i] -= factor * rhs[i - 1];
    }
    std::vector<double> x(n);
    x[n - 1] = rhs[n - 1] / m.diag[n - 1];
    for (std::size_t i = n - 1; i-- > 0;) {
        x[i] = (rhs[i] - m.upper[i] * x[i + 1]) / m.diag[i];
    }
    return x;
}

} // namespace

void validate(const RodProblem& p) {
    requirePositive(p.length, "Rod length L");
    requirePositive(p.area, "Cross-section area A");
    requirePositive(p.conductivity, "Thermal conductivity k");
    requireFinite(p.heatGeneration, "Heat generation q");
    requireFinite(p.tempLeft, "Left temperature T(0)");
    requireFinite(p.tempRight, "Right temperature T(L)");
    if (p.tempLeft < -273.15 || p.tempRight < -273.15) {
        throw std::invalid_argument("Boundary temperatures cannot be below absolute zero (-273.15 C).");
    }
    if (p.elements < 1) {
        throw std::invalid_argument("Number of elements N must be at least 1.");
    }
    if (p.elements > kMaxElements) {
        throw std::invalid_argument("Number of elements N must not exceed " +
                                    std::to_string(kMaxElements) + ".");
    }
}

RodSolution solve(const RodProblem& p) {
    validate(p);

    const std::size_t numElements = static_cast<std::size_t>(p.elements);
    const std::size_t numNodes = numElements + 1;
    const double h = p.length / static_cast<double>(numElements);
    const double ke = p.conductivity * p.area / h;    // element conductance [W/K]
    const double fe = 0.5 * p.heatGeneration * p.area * h; // nodal share of element heat [W]

    // 1. Assemble the global stiffness matrix and load vector element by element.
    Tridiagonal K(numNodes);
    std::vector<double> F(numNodes, 0.0);
    for (std::size_t e = 0; e < numElements; ++e) {
        const std::size_t a = e;     // local node 1
        const std::size_t b = e + 1; // local node 2
        K.diag[a] += ke;
        K.diag[b] += ke;
        K.upper[a] -= ke;
        K.lower[b] -= ke;
        F[a] += fe;
        F[b] += fe;
    }

    RodSolution result;
    result.x.resize(numNodes);
    for (std::size_t i = 0; i < numNodes; ++i) {
        result.x[i] = p.length * static_cast<double>(i) / static_cast<double>(numElements);
    }
    result.T.assign(numNodes, 0.0);
    result.T.front() = p.tempLeft;
    result.T.back() = p.tempRight;

    if (numElements == 1) {
        return result; // both nodes are prescribed; nothing left to solve
    }

    // 2. Impose the prescribed temperatures by eliminating nodes 0 and N:
    //    move K(i,0) T(0) and K(i,N) T(L) to the right-hand side and drop
    //    those rows/columns. This keeps the reduced system symmetric and
    //    leaves the reaction (boundary heat flow) rows out of the solve.
    const std::size_t numFree = numNodes - 2;
    Tridiagonal reduced(numFree);
    std::vector<double> rhs(numFree);
    for (std::size_t i = 0; i < numFree; ++i) {
        const std::size_t g = i + 1; // global node index
        reduced.lower[i] = K.lower[g];
        reduced.diag[i] = K.diag[g];
        reduced.upper[i] = K.upper[g];
        rhs[i] = F[g];
    }
    rhs.front() -= K.lower[1] * p.tempLeft;
    rhs.back() -= K.upper[numNodes - 2] * p.tempRight;
    reduced.lower.front() = 0.0;
    reduced.upper.back() = 0.0;

    // 3. Solve for the free nodal temperatures.
    const std::vector<double> interior = solveTridiagonal(reduced, rhs);
    std::copy(interior.begin(), interior.end(), result.T.begin() + 1);
    return result;
}

double exactTemperature(const RodProblem& p, double x) {
    return p.tempLeft + (p.tempRight - p.tempLeft) * x / p.length +
           p.heatGeneration / (2.0 * p.conductivity) * x * (p.length - x);
}

double interpolate(const RodSolution& s, double x) {
    if (s.x.size() < 2) {
        throw std::invalid_argument("Cannot interpolate a solution with fewer than two nodes.");
    }
    const double x0 = s.x.front();
    const double length = s.x.back() - x0;
    const std::size_t numElements = s.x.size() - 1;
    x = std::clamp(x, x0, s.x.back());

    // Uniform mesh: locate the element directly instead of searching.
    std::size_t e = static_cast<std::size_t>((x - x0) / length * static_cast<double>(numElements));
    e = std::min(e, numElements - 1);

    const double xa = s.x[e];
    const double xb = s.x[e + 1];
    const double xi = (x - xa) / (xb - xa); // local coordinate in [0, 1]
    const double N1 = 1.0 - xi;             // linear shape functions
    const double N2 = xi;
    return N1 * s.T[e] + N2 * s.T[e + 1];
}

ErrorReport evaluateError(const RodProblem& p, const RodSolution& s) {
    ErrorReport report;
    report.elementSize = p.length / static_cast<double>(p.elements);
    for (std::size_t i = 0; i < s.x.size(); ++i) {
        report.maxNodalError =
            std::max(report.maxNodalError, std::abs(s.T[i] - exactTemperature(p, s.x[i])));
    }
    for (std::size_t e = 0; e + 1 < s.x.size(); ++e) {
        const double xm = 0.5 * (s.x[e] + s.x[e + 1]);
        report.maxMidpointError =
            std::max(report.maxMidpointError, std::abs(interpolate(s, xm) - exactTemperature(p, xm)));
    }
    return report;
}

double observedOrder(const ErrorReport& coarse, const ErrorReport& fine) {
    if (coarse.maxMidpointError <= 0.0 || fine.maxMidpointError <= 0.0) {
        return 0.0; // exact on both meshes (e.g. q = 0): order is undefined
    }
    return std::log(coarse.maxMidpointError / fine.maxMidpointError) /
           std::log(coarse.elementSize / fine.elementSize);
}

} // namespace wb::fea
