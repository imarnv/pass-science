#pragma once
// ASCII STL reader with strict structural and numerical validation.
//
// Accepted grammar (keywords case-insensitive, any whitespace layout):
//   solid [name]
//     facet normal nx ny nz
//       outer loop
//         vertex x y z     (exactly three)
//       endloop
//     endfacet
//     ...
//   endsolid [name]
//
// STL carries no unit information; the caller decides how to interpret the
// coordinates (the bundled example is authored in millimetres).

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace wb::io {

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

struct BoundingBox {
    Vec3 min, max;
};

struct StlMesh {
    std::string name;
    // Three vertices per triangle, stored flat: [x0 y0 z0 x1 y1 z1 x2 y2 z2] ...
    std::vector<float> positions;
    // One normal per triangle, recomputed from the vertex winding (the file's
    // stored normal is validated for finiteness but often unreliable).
    std::vector<float> normals;
    std::size_t triangleCount = 0;
    std::size_t degenerateTriangles = 0; // zero-area facets (kept, but reported)
    BoundingBox bounds;
    double surfaceArea = 0.0; // in squared file units
};

// Limit on facets per file; together with kMaxInputBytes this bounds memory.
constexpr std::size_t kMaxStlTriangles = 1'000'000;

// Parses text that is expected to be ASCII STL. Throws ParseError with a
// line number and an actionable message on any malformed input, including
// empty files, binary STL, missing keywords and non-finite coordinates.
StlMesh parseAsciiStl(std::string_view text);

} // namespace wb::io
