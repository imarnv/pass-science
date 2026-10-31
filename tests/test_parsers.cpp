#include "core/io/nodal_csv_parser.h"
#include "core/io/stl_parser.h"
#include "core/io/text_utils.h"
#include "test_framework.h"

#include <fstream>
#include <sstream>

using namespace wb::io;

namespace {
std::string readTestFile(const std::string& name) {
    std::ifstream in(std::string(WB_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    if (!in) throw std::runtime_error("missing test data file " + name);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string readExampleFile(const std::string& name) {
    std::ifstream in(std::string(WB_EXAMPLE_DATA_DIR) + "/" + name, std::ios::binary);
    if (!in) throw std::runtime_error("missing example data file " + name);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

// ----------------------------------------------------------------- helpers

TEST_CASE("text: finite number parsing rejects nan, inf and junk") {
    double v = 0.0;
    CHECK(parseFiniteDouble("1.5e-3", v) && v == 1.5e-3);
    CHECK(parseFiniteDouble("  -42 ", v) && v == -42.0);
    CHECK(!parseFiniteDouble("nan", v));
    CHECK(!parseFiniteDouble("NaN", v));
    CHECK(!parseFiniteDouble("inf", v));
    CHECK(!parseFiniteDouble("-Infinity", v));
    CHECK(!parseFiniteDouble("1e999", v));
    CHECK(!parseFiniteDouble("12abc", v));
    CHECK(!parseFiniteDouble("", v));
}

// --------------------------------------------------------------------- STL

TEST_CASE("stl: valid tetrahedron is parsed with correct geometry") {
    const StlMesh mesh = parseAsciiStl(readTestFile("tetrahedron.stl"));
    CHECK(mesh.name == "tetrahedron");
    CHECK(mesh.triangleCount == 4);
    CHECK(mesh.positions.size() == 4 * 9);
    CHECK(mesh.normals.size() == 4 * 3);
    CHECK(mesh.degenerateTriangles == 0);
    CHECK_NEAR(mesh.bounds.min.x, 0.0, 0.0);
    CHECK_NEAR(mesh.bounds.max.z, 1.0, 0.0);
    // three right-triangle faces of area 0.5 plus an equilateral face of side sqrt(2)
    CHECK_NEAR(mesh.surfaceArea, 1.5 + std::sqrt(3.0) / 2.0, 1e-12);
}

TEST_CASE("stl: bundled example heat sink parses") {
    const StlMesh mesh = parseAsciiStl(readExampleFile("heatsink.stl"));
    CHECK(mesh.triangleCount == 8 * 12); // base plate + 7 fins, 12 triangles per box
    CHECK(mesh.degenerateTriangles == 0);
}

TEST_CASE("stl: whitespace layout and keyword case do not matter") {
    const std::string compact =
        "SOLID x\nFACET NORMAL 0 0 1 OUTER LOOP VERTEX 0 0 0 VERTEX 1 0 0 VERTEX 0 1 0 ENDLOOP ENDFACET ENDSOLID";
    CHECK(parseAsciiStl(compact).triangleCount == 1);
}

TEST_CASE("stl: empty and facet-less files are explained") {
    CHECK_THROWS_WITH(parseAsciiStl(readTestFile("empty.stl")), ParseError, "empty (0 bytes)");
    CHECK_THROWS_WITH(parseAsciiStl("   \n\t\n"), ParseError, "only whitespace");
    CHECK_THROWS_WITH(parseAsciiStl(readTestFile("no_facets.stl")), ParseError, "no facets");
}

TEST_CASE("stl: binary STL is detected and rejected") {
    CHECK_THROWS_WITH(parseAsciiStl(readTestFile("binary.stl")), ParseError, "binary STL");
}

TEST_CASE("stl: non-finite coordinates are rejected with a line number") {
    CHECK_THROWS_WITH(parseAsciiStl(readTestFile("nan_vertex.stl")), ParseError, "Line 5");
    CHECK_THROWS_WITH(parseAsciiStl(readTestFile("inf_vertex.stl")), ParseError, "finite");
}

TEST_CASE("stl: structural errors are rejected") {
    CHECK_THROWS_WITH(parseAsciiStl(readTestFile("missing_vertex.stl")), ParseError, "fewer than three");
    CHECK_THROWS_WITH(parseAsciiStl(readTestFile("truncated.stl")), ParseError, "truncated");
    CHECK_THROWS_WITH(parseAsciiStl(readTestFile("garbage_token.stl")), ParseError, "Expected 'loop'");
    CHECK_THROWS_WITH(parseAsciiStl("hello world"), ParseError, "'solid'");
    const std::string fourVertices =
        "solid q\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\n"
        "vertex 1 1 0\nendloop\nendfacet\nendsolid q\n";
    CHECK_THROWS_WITH(parseAsciiStl(fourVertices), ParseError, "more than three");
}

TEST_CASE("stl: degenerate triangles are counted, not fatal") {
    const std::string degenerate =
        "solid d\nfacet normal 0 0 0\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 2 0 0\n"
        "endloop\nendfacet\nendsolid d\n";
    const StlMesh mesh = parseAsciiStl(degenerate);
    CHECK(mesh.triangleCount == 1);
    CHECK(mesh.degenerateTriangles == 1);
}

// --------------------------------------------------------------------- CSV

TEST_CASE("csv: valid nodal file is parsed with statistics") {
    const NodalDataset d = parseNodalCsv(readTestFile("nodal_valid.csv"));
    CHECK(d.nodes.size() == 4);
    CHECK(d.nodes[2].nodeId == 3);
    CHECK_NEAR(d.nodes[2].temperature, 41.25, 0.0);
    CHECK_NEAR(d.minTemperature, -5.0, 0.0);
    CHECK_NEAR(d.maxTemperature, 41.25, 0.0);
    CHECK_NEAR(d.maxCoord[0], 0.01, 0.0);
}

TEST_CASE("csv: BOM, CRLF, header whitespace and blank lines are tolerated") {
    const NodalDataset d = parseNodalCsv(readTestFile("nodal_bom_crlf.csv"));
    CHECK(d.nodes.size() == 2);
    CHECK(d.skippedBlankLines == 1);
    CHECK_NEAR(d.nodes[1].x, 1e-3, 0.0);
}

TEST_CASE("csv: bundled synthetic example parses") {
    const NodalDataset d = parseNodalCsv(readExampleFile("synthetic_nodal_temperatures.csv"));
    CHECK(d.nodes.size() > 500);
    CHECK(d.maxTemperature > d.minTemperature);
}

TEST_CASE("csv: empty and header-only files are explained") {
    CHECK_THROWS_WITH(parseNodalCsv(readTestFile("nodal_empty.csv")), ParseError, "empty (0 bytes)");
    CHECK_THROWS_WITH(parseNodalCsv(readTestFile("nodal_header_only.csv")), ParseError, "no data rows");
    CHECK_THROWS_WITH(parseNodalCsv("\n\n  \n"), ParseError, "only blank lines");
}

TEST_CASE("csv: malformed rows are rejected with line numbers") {
    CHECK_THROWS_WITH(parseNodalCsv(readTestFile("nodal_bad_header.csv")), ParseError, "Header must be exactly");
    CHECK_THROWS_WITH(parseNodalCsv(readTestFile("nodal_missing_column.csv")), ParseError, "Line 3");
    CHECK_THROWS_WITH(parseNodalCsv(readTestFile("nodal_nan.csv")), ParseError, "temperature_C");
    CHECK_THROWS_WITH(parseNodalCsv(readTestFile("nodal_inf.csv")), ParseError, "y_m");
    CHECK_THROWS_WITH(parseNodalCsv(readTestFile("nodal_duplicate_id.csv")), ParseError, "Duplicate node_id 1");
    CHECK_THROWS_WITH(parseNodalCsv(readTestFile("nodal_non_numeric.csv")), ParseError, "'abc'");
    CHECK_THROWS_WITH(parseNodalCsv(readTestFile("nodal_below_absolute_zero.csv")), ParseError, "absolute zero");
    CHECK_THROWS_WITH(parseNodalCsv(readTestFile("nodal_negative_id.csv")), ParseError, "positive integer");
}

TEST_CASE("csv: binary content is rejected") {
    const std::string withNul("node_id,x_m,y_m,z_m,temperature_C\n1,0,0,0,2\0\n", 44);
    CHECK_THROWS_WITH(parseNodalCsv(withNul), ParseError, "binary");
}
