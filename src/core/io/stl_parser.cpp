#include "core/io/stl_parser.h"

#include "core/io/text_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace wb::io {

namespace {

bool keywordEquals(std::string_view token, std::string_view keyword) {
    if (token.size() != keyword.size()) return false;
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(token[i])) != keyword[i]) return false;
    }
    return true;
}

// Binary STL has an 80-byte header followed by a uint32 triangle count and
// 50 bytes per triangle. Many exporters also write "solid" into that header,
// so the size relationship and non-text bytes are both checked.
bool looksBinary(std::string_view text) {
    if (text.size() >= 84) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
        const std::size_t count = static_cast<std::size_t>(bytes[80]) |
                                  (static_cast<std::size_t>(bytes[81]) << 8) |
                                  (static_cast<std::size_t>(bytes[82]) << 16) |
                                  (static_cast<std::size_t>(bytes[83]) << 24);
        if (84 + count * 50 == text.size()) return true;
    }
    const std::size_t probe = std::min<std::size_t>(text.size(), 1024);
    for (std::size_t i = 0; i < probe; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0 || (c < 9) || (c > 13 && c < 32)) return true;
    }
    return false;
}

// Streams whitespace-separated tokens while tracking the source line, so
// errors can point at the offending line regardless of the file's layout.
class Tokenizer {
public:
    explicit Tokenizer(std::string_view text) : text_(text) {}

    bool next(std::string_view& token) {
        skipSpace();
        if (pos_ >= text_.size()) return false;
        const std::size_t start = pos_;
        while (pos_ < text_.size() && !std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        token = text_.substr(start, pos_ - start);
        tokenLine_ = line_;
        return true;
    }

    // Rest of the current line (used for the free-text solid name).
    std::string_view restOfLine() {
        const std::size_t start = pos_;
        while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
        return trim(text_.substr(start, pos_ - start));
    }

    std::size_t line() const noexcept { return tokenLine_; }

private:
    void skipSpace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            if (text_[pos_] == '\n') ++line_;
            ++pos_;
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t tokenLine_ = 1;
};

class StlReader {
public:
    explicit StlReader(std::string_view text) : tokens_(text) {}

    StlMesh read() {
        std::string_view token;
        if (!tokens_.next(token) || !keywordEquals(token, "solid")) {
            fail("File does not start with the keyword 'solid'. Only ASCII STL is supported.");
        }
        mesh_.name = std::string(tokens_.restOfLine());
        initBounds();

        while (true) {
            if (!tokens_.next(token)) {
                fail("Unexpected end of file: 'endsolid' is missing. The file may be truncated.");
            }
            if (keywordEquals(token, "endsolid")) break;
            if (!keywordEquals(token, "facet")) {
                fail("Expected 'facet' or 'endsolid' but found '" + excerpt(token, 30) + "'.");
            }
            readFacet();
        }

        if (mesh_.triangleCount == 0) {
            throw ParseError("The STL solid contains no facets, so there is no geometry to display. "
                             "Re-export the part with at least one surface triangle.", 0);
        }
        return std::move(mesh_);
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw ParseError("Line " + std::to_string(tokens_.line()) + ": " + message, tokens_.line());
    }

    void expect(std::string_view keyword) {
        std::string_view token;
        if (!tokens_.next(token)) {
            fail("Unexpected end of file while looking for '" + std::string(keyword) +
                 "'. The file may be truncated.");
        }
        if (!keywordEquals(token, keyword)) {
            fail("Expected '" + std::string(keyword) + "' but found '" + excerpt(token, 30) + "'.");
        }
    }

    double readNumber(const char* what) {
        std::string_view token;
        if (!tokens_.next(token)) fail(std::string("Unexpected end of file while reading ") + what + ".");
        double value = 0.0;
        if (!parseFiniteDouble(token, value)) {
            fail(std::string("Invalid ") + what + " value '" + excerpt(token, 30) +
                 "'. Coordinates must be finite decimal numbers (NaN and Inf are rejected).");
        }
        return value;
    }

    Vec3 readVector(const char* what) {
        Vec3 v;
        v.x = readNumber(what);
        v.y = readNumber(what);
        v.z = readNumber(what);
        return v;
    }

    void readFacet() {
        if (mesh_.triangleCount >= kMaxStlTriangles) {
            fail("File exceeds the supported limit of " + std::to_string(kMaxStlTriangles) +
                 " triangles. Decimate the mesh before importing.");
        }
        expect("normal");
        readVector("normal"); // validated but recomputed below
        expect("outer");
        expect("loop");
        std::array<Vec3, 3> vertex;
        for (auto& v : vertex) {
            std::string_view token;
            if (!tokens_.next(token)) fail("Unexpected end of file inside a facet. The file may be truncated.");
            if (keywordEquals(token, "endloop")) {
                fail("Facet has fewer than three vertices. Each STL facet must be a triangle.");
            }
            if (!keywordEquals(token, "vertex")) {
                fail("Expected 'vertex' but found '" + excerpt(token, 30) + "'.");
            }
            v = readVector("vertex coordinate");
        }
        std::string_view token;
        if (!tokens_.next(token)) fail("Unexpected end of file inside a facet. The file may be truncated.");
        if (keywordEquals(token, "vertex")) {
            fail("Facet has more than three vertices. Each STL facet must be a triangle.");
        }
        if (!keywordEquals(token, "endloop")) {
            fail("Expected 'endloop' but found '" + excerpt(token, 30) + "'.");
        }
        expect("endfacet");
        addTriangle(vertex);
    }

    void initBounds() {
        constexpr double inf = std::numeric_limits<double>::infinity();
        mesh_.bounds.min = {inf, inf, inf};
        mesh_.bounds.max = {-inf, -inf, -inf};
    }

    void addTriangle(const std::array<Vec3, 3>& v) {
        const Vec3 e1{v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z};
        const Vec3 e2{v[2].x - v[0].x, v[2].y - v[0].y, v[2].z - v[0].z};
        const Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        const double length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);

        mesh_.surfaceArea += 0.5 * length;
        if (length > 0.0) {
            mesh_.normals.insert(mesh_.normals.end(), {static_cast<float>(n.x / length),
                                                       static_cast<float>(n.y / length),
                                                       static_cast<float>(n.z / length)});
        } else {
            ++mesh_.degenerateTriangles;
            mesh_.normals.insert(mesh_.normals.end(), {0.0f, 0.0f, 0.0f});
        }

        for (const Vec3& p : v) {
            mesh_.positions.insert(mesh_.positions.end(),
                                   {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
            BoundingBox& b = mesh_.bounds;
            b.min = {std::min(b.min.x, p.x), std::min(b.min.y, p.y), std::min(b.min.z, p.z)};
            b.max = {std::max(b.max.x, p.x), std::max(b.max.y, p.y), std::max(b.max.z, p.z)};
        }
        ++mesh_.triangleCount;
    }

    Tokenizer tokens_;
    StlMesh mesh_;
};

} // namespace

StlMesh parseAsciiStl(std::string_view text) {
    if (text.empty()) {
        throw ParseError("The file is empty (0 bytes). Choose an ASCII STL file exported from your CAD tool.", 0);
    }
    if (text.size() > kMaxInputBytes) {
        throw ParseError("The file is larger than the supported 64 MB limit.", 0);
    }
    if (looksBinary(text)) {
        throw ParseError("This looks like a binary STL file. Only ASCII STL is supported: "
                         "re-export the geometry with the 'ASCII' or 'text' STL option.", 0);
    }
    if (trim(text).empty()) {
        throw ParseError("The file contains only whitespace. Choose an ASCII STL file with geometry.", 0);
    }
    return StlReader(text).read();
}

} // namespace wb::io
