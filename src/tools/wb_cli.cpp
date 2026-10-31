// Command-line tool that regenerates the evidence under results/ and the
// example files under data/. Uses the same core library as the application.
//
//   wb_cli fea      [--out DIR]
//   wb_cli backprop [--out DIR]
//   wb_cli bench    [--out DIR] [--cases N] [--elements N] [--workers P] [--reps R] [--seed S]
//   wb_cli datagen  [--out DIR]
//   wb_cli all      [--out DIR] [--data DIR]

#include "core/fea/thermal_rod.h"
#include "core/ml/backprop.h"
#include "core/parallel/batch.h"
#include "core/report/csv_export.h"
#include "core/system/system_info.h"
#include "tools/example_data.h"
#include "tools/svg_plot.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

using namespace wb;

namespace {

const char* const kPalette[] = {"#0a84ff", "#30b158", "#ff9f0a", "#bf5af2", "#ff375f"};

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path + " (does the directory exist?)");
    out << content;
    std::cout << "  wrote " << path << "\n";
}

std::map<std::string, std::string> parseOptions(int argc, char** argv, int first) {
    std::map<std::string, std::string> options;
    for (int i = first; i < argc; ++i) {
        std::string key = argv[i];
        if (key.rfind("--", 0) != 0 || i + 1 >= argc) throw std::invalid_argument("bad option: " + key);
        options[key.substr(2)] = argv[++i];
    }
    return options;
}

std::string option(const std::map<std::string, std::string>& o, const char* key, const std::string& fallback) {
    const auto it = o.find(key);
    return it == o.end() ? fallback : it->second;
}

void runFea(const std::string& out) {
    std::cout << "Thermal FEA mesh study\n";
    const fea::RodProblem base;
    const auto study = report::runMeshStudy(base, {10, 20, 40, 80});
    writeFile(out + "/fea_profiles.csv", report::feaProfileCsv(study));
    writeFile(out + "/fea_convergence.csv", report::feaConvergenceCsv(study));

    tools::PlotSpec profile;
    profile.title = "Temperature along the rod";
    profile.subtitle = "Linear FE (nodes) vs. exact; L = 1 m, k = 10 W/(m K), q = 10 kW/m^3, T(0)=100 C, T(L)=20 C";
    profile.xLabel = "Position x [m]";
    profile.yLabel = "Temperature T [C]";
    tools::Series exact{"Exact", {}, {}, "#1d1d1f", false, true};
    for (int i = 0; i <= 400; ++i) {
        const double x = i / 400.0;
        exact.x.push_back(x);
        exact.y.push_back(fea::exactTemperature(base, x));
    }
    for (std::size_t i = 0; i < study.size(); ++i) {
        profile.series.push_back({"FE, N = " + std::to_string(study[i].problem.elements), study[i].solution.x,
                                  study[i].solution.T, kPalette[i], study[i].problem.elements <= 20, false});
    }
    profile.series.push_back(exact);
    writeFile(out + "/fea_temperature.svg", tools::renderSvg(profile));

    tools::PlotSpec conv;
    conv.title = "Mesh convergence";
    conv.subtitle = "Maximum absolute error at element midpoints (FE interpolation); slope 2 expected";
    conv.xLabel = "Element size h [m]";
    conv.yLabel = "Max midpoint error [C]";
    conv.logX = conv.logY = true;
    tools::Series err{"Max midpoint error", {}, {}, kPalette[0], true, false};
    tools::Series ref{"Reference q h^2 / (8k)", {}, {}, "#8e8e93", false, true};
    for (const auto& e : study) {
        err.x.push_back(e.error.elementSize);
        err.y.push_back(e.error.maxMidpointError);
        ref.x.push_back(e.error.elementSize);
        ref.y.push_back(base.heatGeneration * e.error.elementSize * e.error.elementSize / (8 * base.conductivity));
    }
    conv.series = {err, ref};
    writeFile(out + "/fea_convergence.svg", tools::renderSvg(conv));

    tools::PlotSpec errProfile;
    errProfile.title = "Pointwise error T_fe - T_exact";
    errProfile.subtitle = "Zero at nodes (nodal exactness), maximum q h^2/(8k) at each element midpoint";
    errProfile.xLabel = "Position x [m]";
    errProfile.yLabel = "Error [C]";
    for (std::size_t i = 0; i < study.size(); ++i) {
        tools::Series s{"N = " + std::to_string(study[i].problem.elements), {}, {}, kPalette[i], false, false};
        for (int k = 0; k <= 1600; ++k) {
            const double x = k / 1600.0;
            s.x.push_back(x);
            s.y.push_back(fea::interpolate(study[i].solution, x) - fea::exactTemperature(base, x));
        }
        errProfile.series.push_back(s);
    }
    writeFile(out + "/fea_error_profile.svg", tools::renderSvg(errProfile));

    for (const auto& e : study) {
        std::printf("  N=%3d  h=%.5f  max nodal err=%.3e C  max midpoint err=%.6f C\n", e.problem.elements,
                    e.error.elementSize, e.error.maxNodalError, e.error.maxMidpointError);
    }
}

void runBackprop(const std::string& out) {
    std::cout << "Backpropagation check\n";
    const ml::Params p0;
    const ml::Sample s;
    writeFile(out + "/backprop_gradients.csv", report::backpropGradientCsv(p0, s, 1e-6));
    // The first update is the handwritten one; then 100 subsequent steps.
    const ml::Params p1 = ml::gradientStep(p0, ml::backward(p0, s), 0.1);
    const ml::TrainingTrace trace = ml::train(p1, s, 0.1, 100);
    writeFile(out + "/backprop_loss.csv", report::backpropLossCsv(trace));

    tools::PlotSpec plot;
    plot.title = "Training loss";
    plot.subtitle = "100 gradient-descent steps after the first update, learning rate 0.1, x = 1, y = 2";
    plot.xLabel = "Step after first update [-]";
    plot.yLabel = "Loss J [-] (log scale)";
    plot.logY = true;
    tools::Series loss{"J = 0.5 (y_hat - y)^2", {}, {}, kPalette[0], false, false};
    for (std::size_t k = 0; k < trace.loss.size(); ++k) {
        loss.x.push_back(static_cast<double>(k));
        loss.y.push_back(trace.loss[k]);
    }
    plot.series = {loss};
    writeFile(out + "/backprop_loss.svg", tools::renderSvg(plot));
    std::printf("  J0=%.8f  J after first update=%.8f  J after 100 more steps=%.3e\n", ml::forward(p0, s).loss,
                trace.loss.front(), trace.loss.back());
}

void runBench(const std::string& out, const std::map<std::string, std::string>& o) {
    std::cout << "Multicore benchmark\n";
    parallel::BenchmarkConfig cfg;
    cfg.batch.caseCount = std::stoi(option(o, "cases", std::to_string(cfg.batch.caseCount)));
    cfg.batch.elementsPerCase = std::stoi(option(o, "elements", std::to_string(cfg.batch.elementsPerCase)));
    cfg.batch.seed = std::stoull(option(o, "seed", std::to_string(cfg.batch.seed)));
    cfg.maxWorkers = std::stoi(option(o, "workers", "4"));
    cfg.measuredRuns = std::stoi(option(o, "reps", "5"));
    const auto rep = parallel::runBenchmark(cfg, nullptr, [](const std::string& stage, double f) {
        std::printf("\r  [%3.0f%%] %-28s", f * 100, stage.c_str());
        std::fflush(stdout);
    });
    std::cout << "\n";
    const sys::SystemInfo info = sys::querySystemInfo();
    writeFile(out + "/bench_runs.csv", report::benchmarkRunsCsv(rep));
    writeFile(out + "/bench_summary.csv", report::benchmarkSummaryCsv(rep));
    writeFile(out + "/bench_system.txt", report::systemInfoText(info, &rep));

    tools::PlotSpec plot;
    plot.title = "Parallel speedup";
    plot.subtitle = std::to_string(cfg.batch.caseCount) + " rod solves x " + std::to_string(cfg.batch.elementsPerCase) +
                    " elements; median of " + std::to_string(cfg.measuredRuns) + " runs; " + info.cpuModel;
    plot.xLabel = "Workers p [-]";
    plot.yLabel = "Speedup T1 / Tp [-]";
    tools::Series measured{"Measured", {}, {}, kPalette[0], true, false};
    tools::Series ideal{"Ideal (linear)", {}, {}, "#8e8e93", false, true};
    for (const auto& row : rep.rows) {
        if (row.mode != "parallel") continue;
        measured.x.push_back(row.workers);
        measured.y.push_back(row.speedup);
        ideal.x.push_back(row.workers);
        ideal.y.push_back(row.workers);
    }
    plot.series = {measured, ideal};
    writeFile(out + "/bench_speedup.svg", tools::renderSvg(plot));
    for (const auto& row : rep.rows) {
        std::printf("  %-8s p=%d  median=%.4f s  speedup=%.2f  eff=%.2f  maxdiff=%.1e C\n", row.mode.c_str(),
                    row.workers, row.medianSeconds, row.speedup, row.efficiency, row.maxAbsDiffVsSerial);
    }
    std::cout << "  agreement within 1e-8 C: " << (rep.agreementPassed ? "PASS" : "FAIL") << "\n";
}

void runDatagen(const std::string& out) {
    std::cout << "Example data\n";
    writeFile(out + "/heatsink.stl", tools::heatSinkStl());
    writeFile(out + "/synthetic_nodal_temperatures.csv", tools::syntheticNodalCsv());
}

int usage() {
    std::cerr << "usage: wb_cli {fea|backprop|bench|datagen|all} [--out DIR] [options]\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    try {
        const std::string command = argv[1];
        const auto options = parseOptions(argc, argv, 2);
        const std::string out = option(options, "out", "results");
        if (command == "fea") runFea(out);
        else if (command == "backprop") runBackprop(out);
        else if (command == "bench") runBench(out, options);
        else if (command == "datagen") runDatagen(out);
        else if (command == "all") {
            runDatagen(option(options, "data", "data"));
            runFea(out);
            runBackprop(out);
            runBench(out, options);
        } else return usage();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
