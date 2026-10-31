#pragma once
// Small text helpers shared by the STL and CSV parsers.

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace wb::io {

// Error raised by the file parsers. `line` is 1-based, or 0 when the problem
// concerns the file as a whole (empty, too large, wrong format).
class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& message, std::size_t line)
        : std::runtime_error(message), line_(line) {}
    std::size_t line() const noexcept { return line_; }

private:
    std::size_t line_;
};

// Tested and documented input limit for both formats (see docs/FILE_FORMATS.md).
constexpr std::size_t kMaxInputBytes = 64u * 1024u * 1024u;

std::string_view trim(std::string_view text);

// Splits on `delimiter`, keeping empty fields.
std::vector<std::string_view> split(std::string_view text, char delimiter);

// Splits on runs of ASCII whitespace.
std::vector<std::string_view> splitWhitespace(std::string_view text);

// Parses a complete token as a finite double. Returns false on trailing junk,
// empty input, overflow, NaN or infinity.
bool parseFiniteDouble(std::string_view token, double& out);

// Parses a complete token as a non-negative 64-bit integer.
bool parseUnsigned(std::string_view token, unsigned long long& out);

// Removes a UTF-8 byte-order mark if present.
std::string_view stripBom(std::string_view text);

// Iterates over lines, accepting LF and CRLF endings.
class LineReader {
public:
    explicit LineReader(std::string_view text) : text_(text) {}
    bool next(std::string_view& line);
    std::size_t lineNumber() const noexcept { return lineNumber_; }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
    std::size_t lineNumber_ = 0;
};

// Returns a short, printable excerpt of a line for error messages.
std::string excerpt(std::string_view line, std::size_t maxLength = 60);

} // namespace wb::io
