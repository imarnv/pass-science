#pragma once
// CSV serialisation of computed results. Shared by the application's export
// endpoints and the command-line results tool, so the files a user exports
// from the UI and the files under results/ have identical layouts.

#include "core/fea/thermal_rod.h"
#include "core/ml/backprop.h"
#include "core/parallel/batch.h"
#include "core/system/system_info.h"

#include <string>
#include <vector>

namespace wb::report {

struct MeshStudyEntry {
    fea::RodProblem problem;
    fea::RodSolution solution;
    fea::ErrorReport error;
};

// Runs the solver for each element count and computes errors.
std::vector<MeshStudyEntry> runMeshStudy(const fea::RodProblem& base, const std::vector<int>& elementCounts);

// Long format: one row per (N, sample point); nodes and midpoints are both
// included so the midpoint error can be recomputed from the file.
std::string feaProfileCsv(const std::vector<MeshStudyEntry>& study);

// One row per mesh: N, h, max nodal error, max midpoint error, observed order.
std::string feaConvergenceCsv(const std::vector<MeshStudyEntry>& study);

std::string benchmarkRunsCsv(const parallel::BenchmarkReport& report);
std::string benchmarkSummaryCsv(const parallel::BenchmarkReport& report);
std::string systemInfoText(const sys::SystemInfo& info, const parallel::BenchmarkReport* report);

std::string backpropGradientCsv(const ml::Params& p, const ml::Sample& s, double epsilon);
std::string backpropLossCsv(const ml::TrainingTrace& trace);

// Formats with enough digits to round-trip a double.
std::string formatNumber(double value);

} // namespace wb::report
