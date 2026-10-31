#include "server/json_mapping.h"

#include <cmath>
#include <stdexcept>

namespace wb::server {

namespace {

json vec3(const io::Vec3& v) { return json::array({v.x, v.y, v.z}); }

json errorRow(const fea::ErrorReport& e) {
    return {{"h", e.elementSize}, {"maxNodalError", e.maxNodalError}, {"maxMidpointError", e.maxMidpointError}};
}

json params(const ml::Params& p) { return {{"w", p.w}, {"b", p.b}, {"v", p.v}, {"c", p.c}}; }

json gradients(const ml::Gradients& g) { return {{"w", g.dw}, {"b", g.db}, {"v", g.dv}, {"c", g.dc}}; }

} // namespace

double requireNumber(const json& body, const char* key, double fallback) {
    if (!body.contains(key) || body[key].is_null()) return fallback;
    if (!body[key].is_number()) {
        throw std::invalid_argument(std::string("Field '") + key + "' must be a number.");
    }
    return body[key].get<double>();
}

int requireInt(const json& body, const char* key, int fallback) {
    const double v = requireNumber(body, key, fallback);
    if (std::floor(v) != v || std::abs(v) > 2e9) {
        throw std::invalid_argument(std::string("Field '") + key + "' must be a whole number.");
    }
    return static_cast<int>(v);
}

fea::RodProblem rodProblemFromJson(const json& body) {
    fea::RodProblem p;
    p.length = requireNumber(body, "length", p.length);
    p.area = requireNumber(body, "area", p.area);
    p.conductivity = requireNumber(body, "conductivity", p.conductivity);
    p.heatGeneration = requireNumber(body, "heatGeneration", p.heatGeneration);
    p.tempLeft = requireNumber(body, "tempLeft", p.tempLeft);
    p.tempRight = requireNumber(body, "tempRight", p.tempRight);
    p.elements = requireInt(body, "elements", p.elements);
    return p;
}

parallel::BenchmarkConfig benchmarkConfigFromJson(const json& body) {
    parallel::BenchmarkConfig c;
    const double seed = requireNumber(body, "seed", static_cast<double>(c.batch.seed));
    if (seed < 0 || std::floor(seed) != seed || seed > 9007199254740991.0) {
        throw std::invalid_argument("Seed must be a non-negative whole number.");
    }
    c.batch.seed = static_cast<std::uint64_t>(seed);
    c.batch.caseCount = requireInt(body, "cases", c.batch.caseCount);
    c.batch.elementsPerCase = requireInt(body, "elements", c.batch.elementsPerCase);
    c.maxWorkers = requireInt(body, "maxWorkers", c.maxWorkers);
    c.warmupRuns = requireInt(body, "warmupRuns", c.warmupRuns);
    c.measuredRuns = requireInt(body, "measuredRuns", c.measuredRuns);
    parallel::validate(c.batch);
    return c;
}

json toJson(const sys::SystemInfo& info) {
    return {{"os", info.os},
            {"cpu", info.cpuModel},
            {"physicalCores", info.physicalCores},
            {"logicalCpus", info.logicalCpus},
            {"ramBytes", info.totalRamBytes},
            {"compiler", info.compiler},
            {"buildType", info.buildType},
            {"buildFlags", info.buildFlags},
            {"version", info.appVersion}};
}

json toJson(const io::StlMesh& mesh) {
    return {{"name", mesh.name},
            {"triangles", mesh.triangleCount},
            {"degenerateTriangles", mesh.degenerateTriangles},
            {"surfaceArea", mesh.surfaceArea},
            {"bounds", {{"min", vec3(mesh.bounds.min)}, {"max", vec3(mesh.bounds.max)}}},
            {"positions", base64Encode(mesh.positions.data(), mesh.positions.size() * sizeof(float))},
            {"normals", base64Encode(mesh.normals.data(), mesh.normals.size() * sizeof(float))}};
}

json toJson(const io::NodalDataset& d) {
    std::vector<float> positions;
    std::vector<double> temperatures, ids;
    positions.reserve(d.nodes.size() * 3);
    temperatures.reserve(d.nodes.size());
    ids.reserve(d.nodes.size());
    for (const auto& n : d.nodes) {
        positions.insert(positions.end(),
                         {static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)});
        temperatures.push_back(n.temperature);
        ids.push_back(static_cast<double>(n.nodeId));
    }
    return {{"count", d.nodes.size()},
            {"minTemperature", d.minTemperature},
            {"maxTemperature", d.maxTemperature},
            {"bounds",
             {{"min", {d.minCoord[0], d.minCoord[1], d.minCoord[2]}},
              {"max", {d.maxCoord[0], d.maxCoord[1], d.maxCoord[2]}}}},
            {"skippedBlankLines", d.skippedBlankLines},
            {"positions", base64Encode(positions.data(), positions.size() * sizeof(float))},
            {"temperatures", base64Encode(temperatures.data(), temperatures.size() * sizeof(double))},
            {"ids", base64Encode(ids.data(), ids.size() * sizeof(double))}};
}

json toJson(const std::vector<report::MeshStudyEntry>& study) {
    json meshes = json::array();
    for (std::size_t i = 0; i < study.size(); ++i) {
        const auto& e = study[i];
        json midpoints = json::array();
        for (std::size_t k = 0; k + 1 < e.solution.x.size(); ++k) {
            const double xm = 0.5 * (e.solution.x[k] + e.solution.x[k + 1]);
            midpoints.push_back({xm, fea::interpolate(e.solution, xm), fea::exactTemperature(e.problem, xm)});
        }
        meshes.push_back({{"elements", e.problem.elements},
                          {"x", e.solution.x},
                          {"T", e.solution.T},
                          {"midpoints", midpoints},
                          {"error", errorRow(e.error)},
                          {"order", i > 0 ? json(fea::observedOrder(study[i - 1].error, e.error)) : json()}});
    }
    const fea::RodProblem& p = study.front().problem;
    json exact = json::array();
    for (int i = 0; i <= 200; ++i) {
        const double x = p.length * i / 200.0;
        exact.push_back({x, fea::exactTemperature(p, x)});
    }
    return {{"problem",
             {{"length", p.length}, {"area", p.area}, {"conductivity", p.conductivity},
              {"heatGeneration", p.heatGeneration}, {"tempLeft", p.tempLeft}, {"tempRight", p.tempRight}}},
            {"meshes", meshes},
            {"exact", exact}};
}

json toJson(const parallel::BenchmarkReport& r) {
    json rows = json::array();
    for (const auto& row : r.rows) {
        rows.push_back({{"mode", row.mode},
                        {"workers", row.workers},
                        {"runs", row.runSeconds},
                        {"median", row.medianSeconds},
                        {"speedup", row.speedup},
                        {"efficiency", row.efficiency},
                        {"maxAbsDiff", row.maxAbsDiffVsSerial}});
    }
    const auto& c = r.config;
    return {{"config",
             {{"seed", c.batch.seed}, {"cases", c.batch.caseCount}, {"elements", c.batch.elementsPerCase},
              {"kRange", {c.batch.kMin, c.batch.kMax}}, {"qRange", {c.batch.qMin, c.batch.qMax}},
              {"maxWorkers", c.maxWorkers}, {"warmupRuns", c.warmupRuns}, {"measuredRuns", c.measuredRuns}}},
            {"logicalCpus", r.logicalCpus},
            {"rows", rows},
            {"tolerance", r.toleranceC},
            {"agreementPassed", r.agreementPassed},
            {"note", r.note}};
}

json toJson(const hw::DeviceInfo& d) {
    return {{"id", d.id},
            {"kind", hw::toString(d.kind)},
            {"model", d.model},
            {"transport", d.transport},
            {"port", d.port},
            {"firmware", d.firmware},
            {"ioVoltage", d.ioVoltage},
            {"capabilities", d.capabilities},
            {"simulated", d.simulated}};
}

json toJson(const hw::Telemetry& t) {
    json values = json::object();
    for (const auto& [name, value] : t.values) {
        values[name] = {{"value", value}, {"unit", t.units.at(name)}};
    }
    return {{"deviceId", t.deviceId}, {"sequence", t.sequence}, {"timestampMs", t.timestampMs},
            {"values", values}, {"simulated", true}};
}

json toJson(const hw::DeviceEvent& e) {
    return {{"timestampMs", e.timestampMs}, {"deviceId", e.deviceId}, {"level", e.level}, {"message", e.message}};
}

json backpropToJson(const ml::Params& p0, const ml::Sample& s, double learningRate, int steps) {
    const ml::Forward f = ml::forward(p0, s);
    const ml::Gradients g = ml::backward(p0, s);
    const ml::Gradients n = ml::numericalGradients(p0, s, 1e-6);
    const ml::Params p1 = ml::gradientStep(p0, g, learningRate);
    const ml::TrainingTrace trace = ml::train(p1, s, learningRate, steps);
    const double delta = f.yHat - s.y;
    return {{"sample", {{"x", s.x}, {"y", s.y}}},
            {"learningRate", learningRate},
            {"initial", params(p0)},
            {"forward", {{"z", f.z}, {"h", f.h}, {"yHat", f.yHat}, {"loss", f.loss}}},
            {"intermediate",
             {{"dJ_dyhat", delta}, {"dJ_dh", delta * p0.v}, {"dh_dz", 1.0 - f.h * f.h},
              {"dJ_dz", delta * p0.v * (1.0 - f.h * f.h)}}},
            {"analytic", gradients(g)},
            {"numerical", gradients(n)},
            {"epsilon", 1e-6},
            {"updated", params(p1)},
            {"updatedLoss", ml::forward(p1, s).loss},
            {"loss", trace.loss}};
}

std::string base64Encode(const void* data, std::size_t size) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < size; i += 3) {
        const unsigned v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += alphabet[(v >> 6) & 63];
        out += alphabet[v & 63];
    }
    if (i < size) {
        unsigned v = bytes[i] << 16;
        if (i + 1 < size) v |= bytes[i + 1] << 8;
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += i + 1 < size ? alphabet[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

} // namespace wb::server
