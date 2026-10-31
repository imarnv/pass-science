#pragma once
// Reader for nodal temperature results exported to CSV.
//
// Required schema (header row, exact column names and order):
//     node_id,x_m,y_m,z_m,temperature_C
// node_id: positive integer, unique;  x_m, y_m, z_m: metres;  temperature_C: degrees C.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace wb::io {

struct NodalResult {
    std::uint64_t nodeId = 0;
    double x = 0.0, y = 0.0, z = 0.0; // [m]
    double temperature = 0.0;         // [C]
};

struct NodalDataset {
    std::vector<NodalResult> nodes;
    double minTemperature = 0.0;
    double maxTemperature = 0.0;
    double minCoord[3] = {0, 0, 0};
    double maxCoord[3] = {0, 0, 0};
    std::size_t skippedBlankLines = 0;
};

constexpr std::size_t kMaxCsvNodes = 2'000'000;

// Throws ParseError with a line number and an actionable message on any
// malformed input (bad header, wrong field count, non-numeric or non-finite
// values, duplicate node ids, temperatures below absolute zero, empty file).
NodalDataset parseNodalCsv(std::string_view text);

} // namespace wb::io
