#include "core/report/csv_export.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace wb::report {

std::string formatNumber(double value) {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

std::vector<MeshStudyEntry> runMeshStudy(const fea::RodProblem& base, const std::vector<int>& elementCounts) {
    if (elementCounts.empty()) throw std::invalid_argument("Select at least one mesh size.");
    std::vector<MeshStudyEntry> study;
    for (int n : elementCounts) {
        MeshStudyEntry entry;
        entry.problem = base;
        entry.problem.elements = n;
        entry.solution = fea::solve(entry.problem);
        entry.error = fea::evaluateError(entry.problem, entry.solution);
        study.push_back(std::move(entry));
    }
    return study;
}

std::string feaProfileCsv(const std::vector<MeshStudyEntry>& study) {
    std::ostringstream out;
    out << "elements,point_type,x_m,T_fe_C,T_exact_C,abs_error_C\n";
    for (const MeshStudyEntry& e : study) {
        const auto& s = e.solution;
        for (std::size_t i = 0; i < s.x.size(); ++i) {
            const double exact = fea::exactTemperature(e.problem, s.x[i]);
            out << e.problem.elements << ",node," << formatNumber(s.x[i]) << ',' << formatNumber(s.T[i]) << ','
                << formatNumber(exact) << ',' << formatNumber(std::abs(s.T[i] - exact)) << '\n';
            if (i + 1 < s.x.size()) {
                const double xm = 0.5 * (s.x[i] + s.x[i + 1]);
                const double fe = fea::interpolate(s, xm);
                const double ex = fea::exactTemperature(e.problem, xm);
                out << e.problem.elements << ",midpoint," << formatNumber(xm) << ',' << formatNumber(fe) << ','
                    << formatNumber(ex) << ',' << formatNumber(std::abs(fe - ex)) << '\n';
            }
        }
    }
    return out.str();
}

std::string feaConvergenceCsv(const std::vector<MeshStudyEntry>& study) {
    std::ostringstream out;
    out << "elements,h_m,max_nodal_error_C,max_midpoint_error_C,observed_order\n";
    for (std::size_t i = 0; i < study.size(); ++i) {
        const auto& e = study[i];
        out << e.problem.elements << ',' << formatNumber(e.error.elementSize) << ','
            << formatNumber(e.error.maxNodalError) << ',' << formatNumber(e.error.maxMidpointError) << ',';
        if (i > 0) out << formatNumber(fea::observedOrder(study[i - 1].error, e.error));
        out << '\n';
    }
    return out.str();
}

std::string benchmarkRunsCsv(const parallel::BenchmarkReport& report) {
    std::ostringstream out;
    out << "mode,workers,repetition,wall_seconds\n";
    for (const auto& row : report.rows) {
        for (std::size_t r = 0; r < row.runSeconds.size(); ++r) {
            out << row.mode << ',' << row.workers << ',' << r + 1 << ',' << formatNumber(row.runSeconds[r]) << '\n';
        }
    }
    return out.str();
}

std::string benchmarkSummaryCsv(const parallel::BenchmarkReport& report) {
    std::ostringstream out;
    out << "mode,workers,median_seconds,speedup_T1_over_Tp,efficiency,max_abs_diff_vs_serial_C,agreement_within_1e-8\n";
    for (const auto& row : report.rows) {
        out << row.mode << ',' << row.workers << ',' << formatNumber(row.medianSeconds) << ','
            << formatNumber(row.speedup) << ',' << formatNumber(row.efficiency) << ','
            << formatNumber(row.maxAbsDiffVsSerial) << ','
            << (row.maxAbsDiffVsSerial <= report.toleranceC ? "yes" : "no") << '\n';
    }
    return out.str();
}

std::string systemInfoText(const sys::SystemInfo& info, const parallel::BenchmarkReport* report) {
    std::ostringstream out;
    out << "os: " << info.os << '\n'
        << "cpu: " << info.cpuModel << '\n'
        << "physical_cores: " << (info.physicalCores ? std::to_string(info.physicalCores) : "unknown") << '\n'
        << "logical_cpus: " << info.logicalCpus << '\n'
        << "ram_gib: " << formatNumber(static_cast<double>(info.totalRamBytes) / (1024.0 * 1024.0 * 1024.0)) << '\n'
        << "compiler: " << info.compiler << '\n'
        << "build_type: " << info.buildType << '\n'
        << "optimisation_flags: " << info.buildFlags << '\n'
        << "app_version: " << info.appVersion << '\n';
    if (report) {
        const auto& c = report->config;
        out << "workload: " << c.batch.caseCount << " independent rod solves x " << c.batch.elementsPerCase
            << " linear elements, seed " << c.batch.seed << ", k in [" << c.batch.kMin << ", " << c.batch.kMax
            << "] W/(m K), q in [" << c.batch.qMin << ", " << c.batch.qMax << "] W/m^3\n"
            << "warmup_runs: " << c.warmupRuns << '\n'
            << "measured_runs: " << c.measuredRuns << '\n'
            << "timer: std::chrono::steady_clock, wall-clock, solve + result storage only "
               "(case generation, file I/O and rendering excluded)\n"
            << "agreement_tolerance_C: " << report->toleranceC << '\n'
            << "agreement_passed: " << (report->agreementPassed ? "yes" : "no") << '\n';
        if (!report->note.empty()) out << "note: " << report->note << '\n';
    }
    return out.str();
}

std::string backpropGradientCsv(const ml::Params& p, const ml::Sample& s, double epsilon) {
    const ml::Gradients a = ml::backward(p, s);
    const ml::Gradients n = ml::numericalGradients(p, s, epsilon);
    std::ostringstream out;
    out << "parameter,value,analytic_gradient,central_difference_gradient,abs_error,passes_1e-5\n";
    auto row = [&](const char* name, double value, double ga, double gn) {
        out << name << ',' << formatNumber(value) << ',' << formatNumber(ga) << ',' << formatNumber(gn) << ','
            << formatNumber(std::abs(ga - gn)) << ',' << (std::abs(ga - gn) <= 1e-5 ? "yes" : "no") << '\n';
    };
    row("w", p.w, a.dw, n.dw);
    row("b", p.b, a.db, n.db);
    row("v", p.v, a.dv, n.dv);
    row("c", p.c, a.dc, n.dc);
    return out.str();
}

std::string backpropLossCsv(const ml::TrainingTrace& trace) {
    std::ostringstream out;
    out << "step,loss,w,b,v,c\n";
    for (std::size_t k = 0; k < trace.loss.size(); ++k) {
        const auto& p = trace.params[k];
        out << k << ',' << formatNumber(trace.loss[k]) << ',' << formatNumber(p.w) << ',' << formatNumber(p.b)
            << ',' << formatNumber(p.v) << ',' << formatNumber(p.c) << '\n';
    }
    return out.str();
}

} // namespace wb::report
