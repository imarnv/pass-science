#pragma once
// Conversion between core types and the JSON used by the HTTP API.
// Kept separate so the core library has no JSON or transport dependency.

#include "core/fea/thermal_rod.h"
#include "core/hardware/device_manager.h"
#include "core/io/nodal_csv_parser.h"
#include "core/io/stl_parser.h"
#include "core/ml/backprop.h"
#include "core/parallel/batch.h"
#include "core/report/csv_export.h"
#include "core/system/system_info.h"

#include <nlohmann/json.hpp>

namespace wb::server {

using nlohmann::json;

// Reads a required/optional number from a request body with a clear error.
double requireNumber(const json& body, const char* key, double fallback);
int requireInt(const json& body, const char* key, int fallback);

fea::RodProblem rodProblemFromJson(const json& body);
parallel::BenchmarkConfig benchmarkConfigFromJson(const json& body);

json toJson(const sys::SystemInfo& info);
json toJson(const io::StlMesh& mesh);         // geometry as base64 float32 arrays
json toJson(const io::NodalDataset& dataset); // nodes as base64 float32/float64 arrays
json toJson(const std::vector<report::MeshStudyEntry>& study);
json toJson(const parallel::BenchmarkReport& report);
json toJson(const hw::DeviceInfo& info);
json toJson(const hw::Telemetry& telemetry);
json toJson(const hw::DeviceEvent& event);
json backpropToJson(const ml::Params& p, const ml::Sample& s, double learningRate, int steps);

std::string base64Encode(const void* data, std::size_t size);

} // namespace wb::server
