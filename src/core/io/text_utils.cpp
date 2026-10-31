#include "core/io/text_utils.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace wb::io {

namespace {
bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
} // namespace

std::string_view trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && isSpace(text[begin])) ++begin;
    while (end > begin && isSpace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

std::vector<std::string_view> split(std::string_view text, char delimiter) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = text.find(delimiter, start);
        if (pos == std::string_view::npos) {
            fields.push_back(text.substr(start));
            return fields;
        }
        fields.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
}

std::vector<std::string_view> splitWhitespace(std::string_view text) {
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isSpace(text[i])) ++i;
        const std::size_t start = i;
        while (i < text.size() && !isSpace(text[i])) ++i;
        if (i > start) tokens.push_back(text.substr(start, i - start));
    }
    return tokens;
}

bool parseFiniteDouble(std::string_view token, double& out) {
    token = trim(token);
    if (token.empty() || token.size() > 64) return false;
    // strtod needs a null-terminated buffer; tokens are short so copy locally.
    char buffer[65];
    token.copy(buffer, token.size());
    buffer[token.size()] = '\0';
    // Reject textual nan/inf spellings explicitly, regardless of locale/libc.
    for (std::size_t i = 0; i < token.size(); ++i) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(buffer[i])));
        if (c == 'n' || c == 'i') return false;
    }
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(buffer, &end);
    if (end != buffer + token.size() || errno == ERANGE || !std::isfinite(value)) return false;
    out = value;
    return true;
}

bool parseUnsigned(std::string_view token, unsigned long long& out) {
    token = trim(token);
    if (token.empty() || token.size() > 19) return false;
    unsigned long long value = 0;
    for (char c : token) {
        if (c < '0' || c > '9') return false;
        value = value * 10ULL + static_cast<unsigned long long>(c - '0');
    }
    out = value;
    return true;
}

std::string_view stripBom(std::string_view text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        return text.substr(3);
    }
    return text;
}

bool LineReader::next(std::string_view& line) {
    if (pos_ >= text_.size()) return false;
    std::size_t end = text_.find('\n', pos_);
    if (end == std::string_view::npos) end = text_.size();
    line = text_.substr(pos_, end - pos_);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    pos_ = end + 1;
    ++lineNumber_;
    return true;
}

std::string excerpt(std::string_view line, std::size_t maxLength) {
    std::string out;
    for (char c : line.substr(0, maxLength)) {
        const auto uc = static_cast<unsigned char>(c);
        out.push_back(uc >= 32 && uc < 127 ? c : '?');
    }
    if (line.size() > maxLength) out += "...";
    return out;
}

} // namespace wb::io
