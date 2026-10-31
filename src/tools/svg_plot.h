#pragma once
// Minimal SVG line-plot writer for the results/ figures (no plotting dependency).

#include <string>
#include <vector>

namespace wb::tools {

struct Series {
    std::string label;
    std::vector<double> x, y;
    std::string color = "#0a84ff";
    bool markers = false;
    bool dashed = false;
};

struct PlotSpec {
    std::string title;
    std::string subtitle;
    std::string xLabel; // include units, e.g. "Position x [m]"
    std::string yLabel;
    bool logX = false, logY = false;
    int width = 820, height = 500;
    std::vector<Series> series;
};

std::string renderSvg(const PlotSpec& spec);

} // namespace wb::tools
