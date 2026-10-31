#include "tools/example_data.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

namespace wb::tools {

namespace {

struct Box {
    double x0, y0, z0, x1, y1, z1;
};

// Heat sink layout shared by both generators (millimetres).
constexpr double kBase = 40.0, kBaseThickness = 4.0, kFinThickness = 1.6, kFinHeight = 18.0;
constexpr int kFinCount = 7;

std::vector<Box> heatSinkBoxes() {
    std::vector<Box> boxes{{0, 0, 0, kBase, kBase, kBaseThickness}};
    const double pitch = (kBase - kFinThickness) / (kFinCount - 1);
    for (int i = 0; i < kFinCount; ++i) {
        const double x = i * pitch;
        boxes.push_back({x, 0, kBaseThickness, x + kFinThickness, kBase, kBaseThickness + kFinHeight});
    }
    return boxes;
}

void writeFacet(std::ostringstream& o, std::array<double, 3> n, const std::array<std::array<double, 3>, 3>& v) {
    char line[160];
    std::snprintf(line, sizeof(line), "  facet normal %g %g %g\n    outer loop\n", n[0], n[1], n[2]);
    o << line;
    for (const auto& p : v) {
        std::snprintf(line, sizeof(line), "      vertex %.6f %.6f %.6f\n", p[0], p[1], p[2]);
        o << line;
    }
    o << "    endloop\n  endfacet\n";
}

void writeBox(std::ostringstream& o, const Box& b) {
    // 8 corners, 6 faces x 2 triangles, counter-clockwise seen from outside.
    const std::array<std::array<double, 3>, 8> c{{{b.x0, b.y0, b.z0}, {b.x1, b.y0, b.z0}, {b.x1, b.y1, b.z0},
                                                  {b.x0, b.y1, b.z0}, {b.x0, b.y0, b.z1}, {b.x1, b.y0, b.z1},
                                                  {b.x1, b.y1, b.z1}, {b.x0, b.y1, b.z1}}};
    struct Face { int a, b, c, d; std::array<double, 3> n; };
    const Face faces[6] = {{0, 3, 2, 1, {0, 0, -1}}, {4, 5, 6, 7, {0, 0, 1}}, {0, 1, 5, 4, {0, -1, 0}},
                           {2, 3, 7, 6, {0, 1, 0}},  {1, 2, 6, 5, {1, 0, 0}}, {3, 0, 4, 7, {-1, 0, 0}}};
    for (const Face& f : faces) {
        writeFacet(o, f.n, {c[f.a], c[f.b], c[f.c]});
        writeFacet(o, f.n, {c[f.a], c[f.c], c[f.d]});
    }
}

bool insideHeatSink(double x, double y, double z) {
    for (const Box& b : heatSinkBoxes()) {
        constexpr double eps = 1e-9;
        if (x >= b.x0 - eps && x <= b.x1 + eps && y >= b.y0 - eps && y <= b.y1 + eps && z >= b.z0 - eps &&
            z <= b.z1 + eps)
            return true;
    }
    return false;
}

} // namespace

std::string heatSinkStl() {
    std::ostringstream o;
    o << "solid finned_heat_sink_mm\n";
    for (const Box& b : heatSinkBoxes()) writeBox(o, b);
    o << "endsolid finned_heat_sink_mm\n";
    return o.str();
}

std::string syntheticNodalCsv() {
    std::ostringstream o;
    o << "node_id,x_m,y_m,z_m,temperature_C\n";
    const double ambient = 25.0, rise = 62.0;
    const double sigma = 11.0;   // mm, footprint of the heat source under the base
    const double decay = 14.0;   // mm, decay length up the fins
    const double pitch = (kBase - kFinThickness) / (kFinCount - 1);

    // Sample x on the fin faces and fin mid-planes so every fin is visible.
    std::vector<double> xs;
    for (int i = 0; i < kFinCount; ++i) {
        const double x = i * pitch;
        xs.insert(xs.end(), {x, x + kFinThickness / 2, x + kFinThickness});
        if (i + 1 < kFinCount) xs.push_back(x + kFinThickness + (pitch - kFinThickness) / 2);
    }
    unsigned long id = 1;
    char line[128];
    for (double x : xs) {
        for (double y = 0.0; y <= kBase + 1e-9; y += 2.5) {
            for (double z = 0.0; z <= kBaseThickness + kFinHeight + 1e-9; z += 1.0) {
                if (!insideHeatSink(x, y, z)) continue;
                const double r2 = (x - kBase / 2) * (x - kBase / 2) + (y - kBase / 2) * (y - kBase / 2);
                const double t = ambient + rise * std::exp(-r2 / (2 * sigma * sigma)) * std::exp(-z / decay) +
                                 4.0 * std::exp(-z / 3.0);
                std::snprintf(line, sizeof(line), "%lu,%.6f,%.6f,%.6f,%.4f\n", id++, x / 1000.0, y / 1000.0,
                              z / 1000.0, t);
                o << line;
            }
        }
    }
    return o.str();
}

} // namespace wb::tools
