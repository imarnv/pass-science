#include "tools/svg_plot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>

namespace wb::tools {

namespace {

std::string fmt(double v, const char* pattern = "%.4g") {
    char buf[40];
    std::snprintf(buf, sizeof(buf), pattern, v);
    return buf;
}

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        default: out += c;
        }
    }
    return out;
}

// "Nice" linear tick positions covering [lo, hi].
std::vector<double> linearTicks(double lo, double hi, int target = 6) {
    const double span = hi - lo;
    const double raw = span / target;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double residual = raw / magnitude;
    const double step = magnitude * (residual > 5 ? 10 : residual > 2 ? 5 : residual > 1 ? 2 : 1);
    std::vector<double> ticks;
    for (double t = std::ceil(lo / step) * step; t <= hi + step * 1e-9; t += step) {
        ticks.push_back(std::abs(t) < step * 1e-9 ? 0.0 : t);
    }
    return ticks;
}

struct Axis {
    double lo = 0, hi = 1;
    bool log = false;
    double map(double v, double p0, double p1) const {
        const double a = log ? std::log10(lo) : lo;
        const double b = log ? std::log10(hi) : hi;
        const double t = ((log ? std::log10(v) : v) - a) / (b - a);
        return p0 + t * (p1 - p0);
    }
    std::vector<double> ticks() const {
        if (!log) return linearTicks(lo, hi);
        std::vector<double> t;
        for (double e = std::floor(std::log10(lo)); e <= std::ceil(std::log10(hi)); ++e) {
            const double v = std::pow(10.0, e);
            if (v >= lo * 0.999 && v <= hi * 1.001) t.push_back(v);
        }
        return t;
    }
};

Axis fitAxis(const std::vector<Series>& series, bool useX, bool log) {
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (const auto& s : series) {
        for (double v : useX ? s.x : s.y) {
            if (log && v <= 0) continue;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    Axis a;
    a.log = log;
    if (!std::isfinite(lo)) { lo = 0; hi = 1; }
    if (log) {
        a.lo = std::pow(10.0, std::floor(std::log10(lo)));
        a.hi = std::pow(10.0, std::ceil(std::log10(hi)));
        if (a.lo == a.hi) a.hi *= 10;
    } else {
        if (hi == lo) { hi += 1; lo -= 1; }
        const auto ticks = linearTicks(lo, hi);
        const double step = ticks.size() > 1 ? ticks[1] - ticks[0] : (hi - lo);
        a.lo = std::floor(lo / step) * step;
        a.hi = std::ceil(hi / step) * step;
    }
    return a;
}

} // namespace

std::string renderSvg(const PlotSpec& spec) {
    const double left = 84, right = spec.width - 190.0, top = 74, bottom = spec.height - 64.0;
    const Axis ax = fitAxis(spec.series, true, spec.logX);
    const Axis ay = fitAxis(spec.series, false, spec.logY);
    std::ostringstream o;
    o << "<svg xmlns='http://www.w3.org/2000/svg' width='" << spec.width << "' height='" << spec.height
      << "' viewBox='0 0 " << spec.width << ' ' << spec.height << "' font-family='Helvetica, Arial, sans-serif'>\n"
      << "<rect width='100%' height='100%' fill='#ffffff'/>\n"
      << "<text x='" << left << "' y='34' font-size='19' font-weight='600' fill='#1d1d1f'>" << escape(spec.title)
      << "</text>\n"
      << "<text x='" << left << "' y='56' font-size='13' fill='#6e6e73'>" << escape(spec.subtitle) << "</text>\n";

    // grid + tick labels
    for (double t : ax.ticks()) {
        const double px = ax.map(t, left, right);
        o << "<line x1='" << fmt(px) << "' y1='" << top << "' x2='" << fmt(px) << "' y2='" << bottom
          << "' stroke='#e5e5ea'/>\n<text x='" << fmt(px) << "' y='" << bottom + 20
          << "' font-size='12' fill='#6e6e73' text-anchor='middle'>" << fmt(t) << "</text>\n";
    }
    for (double t : ay.ticks()) {
        const double py = ay.map(t, bottom, top);
        o << "<line x1='" << left << "' y1='" << fmt(py) << "' x2='" << right << "' y2='" << fmt(py)
          << "' stroke='#e5e5ea'/>\n<text x='" << left - 10 << "' y='" << fmt(py + 4)
          << "' font-size='12' fill='#6e6e73' text-anchor='end'>" << fmt(t) << "</text>\n";
    }
    o << "<rect x='" << left << "' y='" << top << "' width='" << right - left << "' height='" << bottom - top
      << "' fill='none' stroke='#c7c7cc'/>\n";
    o << "<text x='" << (left + right) / 2 << "' y='" << spec.height - 18
      << "' font-size='13' fill='#1d1d1f' text-anchor='middle'>" << escape(spec.xLabel) << "</text>\n";
    o << "<text transform='translate(22 " << (top + bottom) / 2
      << ") rotate(-90)' font-size='13' fill='#1d1d1f' text-anchor='middle'>" << escape(spec.yLabel) << "</text>\n";

    // series
    int legendRow = 0;
    for (const auto& s : spec.series) {
        o << "<polyline fill='none' stroke='" << s.color << "' stroke-width='2'"
          << (s.dashed ? " stroke-dasharray='6 4'" : "") << " points='";
        for (std::size_t i = 0; i < s.x.size(); ++i) {
            if ((spec.logX && s.x[i] <= 0) || (spec.logY && s.y[i] <= 0)) continue;
            o << fmt(ax.map(s.x[i], left, right), "%.2f") << ',' << fmt(ay.map(s.y[i], bottom, top), "%.2f") << ' ';
        }
        o << "'/>\n";
        if (s.markers) {
            for (std::size_t i = 0; i < s.x.size(); ++i) {
                if ((spec.logX && s.x[i] <= 0) || (spec.logY && s.y[i] <= 0)) continue;
                o << "<circle cx='" << fmt(ax.map(s.x[i], left, right), "%.2f") << "' cy='"
                  << fmt(ay.map(s.y[i], bottom, top), "%.2f") << "' r='3.5' fill='#fff' stroke='" << s.color
                  << "' stroke-width='2'/>\n";
            }
        }
        const double ly = top + 10 + legendRow * 24.0;
        o << "<line x1='" << right + 20 << "' y1='" << ly << "' x2='" << right + 44 << "' y2='" << ly
          << "' stroke='" << s.color << "' stroke-width='2'" << (s.dashed ? " stroke-dasharray='6 4'" : "")
          << "/>\n<text x='" << right + 52 << "' y='" << ly + 4 << "' font-size='12' fill='#1d1d1f'>"
          << escape(s.label) << "</text>\n";
        ++legendRow;
    }
    o << "</svg>\n";
    return o.str();
}

} // namespace wb::tools
