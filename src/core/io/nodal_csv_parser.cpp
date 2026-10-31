#include "core/io/nodal_csv_parser.h"

#include "core/io/text_utils.h"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_set>

namespace wb::io {

namespace {

constexpr std::array<std::string_view, 5> kColumns = {"node_id", "x_m", "y_m", "z_m", "temperature_C"};
constexpr double kAbsoluteZeroC = -273.15;

std::string expectedHeader() {
    std::string header;
    for (std::size_t i = 0; i < kColumns.size(); ++i) {
        if (i) header += ',';
        header += kColumns[i];
    }
    return header;
}

[[noreturn]] void fail(std::size_t line, const std::string& message) {
    throw ParseError("Line " + std::to_string(line) + ": " + message, line);
}

void checkHeader(std::string_view line, std::size_t lineNumber) {
    const auto fields = split(line, ',');
    bool ok = fields.size() == kColumns.size();
    for (std::size_t i = 0; ok && i < fields.size(); ++i) {
        ok = trim(fields[i]) == kColumns[i];
    }
    if (!ok) {
        fail(lineNumber, "Header must be exactly '" + expectedHeader() + "' but was '" + excerpt(line) +
                             "'. Rename or reorder the columns in your export.");
    }
}

double parseField(std::string_view field, std::size_t column, std::size_t line) {
    double value = 0.0;
    if (!parseFiniteDouble(field, value)) {
        fail(line, "Column '" + std::string(kColumns[column]) + "' has invalid value '" +
                       excerpt(trim(field), 30) + "'. Expected a finite number (NaN and Inf are rejected).");
    }
    return value;
}

} // namespace

NodalDataset parseNodalCsv(std::string_view text) {
    if (text.empty()) {
        throw ParseError("The file is empty (0 bytes). Expected a CSV with header '" + expectedHeader() +
                             "' followed by one row per node.", 0);
    }
    if (text.size() > kMaxInputBytes) {
        throw ParseError("The file is larger than the supported 64 MB limit.", 0);
    }
    if (text.find('\0') != std::string_view::npos) {
        throw ParseError("The file contains binary data. Expected a plain-text CSV export.", 0);
    }

    LineReader reader(stripBom(text));
    std::string_view line;
    NodalDataset data;

    // Header: first non-blank line.
    bool haveHeader = false;
    while (reader.next(line)) {
        if (trim(line).empty()) continue;
        checkHeader(line, reader.lineNumber());
        haveHeader = true;
        break;
    }
    if (!haveHeader) {
        throw ParseError("The file contains only blank lines. Expected header '" + expectedHeader() + "'.", 0);
    }

    std::unordered_set<std::uint64_t> seenIds;
    while (reader.next(line)) {
        const std::size_t lineNumber = reader.lineNumber();
        if (trim(line).empty()) {
            ++data.skippedBlankLines;
            continue;
        }
        const auto fields = split(line, ',');
        if (fields.size() != kColumns.size()) {
            fail(lineNumber, "Expected 5 comma-separated values but found " + std::to_string(fields.size()) +
                                 ": '" + excerpt(line) + "'.");
        }
        if (data.nodes.size() >= kMaxCsvNodes) {
            fail(lineNumber, "File exceeds the supported limit of " + std::to_string(kMaxCsvNodes) + " nodes.");
        }

        NodalResult node;
        unsigned long long id = 0;
        if (!parseUnsigned(fields[0], id) || id == 0) {
            fail(lineNumber, "node_id '" + excerpt(trim(fields[0]), 30) + "' must be a positive integer.");
        }
        node.nodeId = id;
        if (!seenIds.insert(node.nodeId).second) {
            fail(lineNumber, "Duplicate node_id " + std::to_string(node.nodeId) + ". Node ids must be unique.");
        }
        node.x = parseField(fields[1], 1, lineNumber);
        node.y = parseField(fields[2], 2, lineNumber);
        node.z = parseField(fields[3], 3, lineNumber);
        node.temperature = parseField(fields[4], 4, lineNumber);
        if (node.temperature < kAbsoluteZeroC) {
            fail(lineNumber, "temperature_C " + std::to_string(node.temperature) +
                                 " is below absolute zero. Check that the export unit is degrees Celsius.");
        }
        data.nodes.push_back(node);
    }

    if (data.nodes.empty()) {
        throw ParseError("The file has a valid header but no data rows, so there are no nodes to display.", 0);
    }

    const NodalResult& first = data.nodes.front();
    data.minTemperature = data.maxTemperature = first.temperature;
    data.minCoord[0] = data.maxCoord[0] = first.x;
    data.minCoord[1] = data.maxCoord[1] = first.y;
    data.minCoord[2] = data.maxCoord[2] = first.z;
    for (const NodalResult& n : data.nodes) {
        data.minTemperature = std::min(data.minTemperature, n.temperature);
        data.maxTemperature = std::max(data.maxTemperature, n.temperature);
        const double c[3] = {n.x, n.y, n.z};
        for (int i = 0; i < 3; ++i) {
            data.minCoord[i] = std::min(data.minCoord[i], c[i]);
            data.maxCoord[i] = std::max(data.maxCoord[i], c[i]);
        }
    }
    return data;
}

} // namespace wb::io
