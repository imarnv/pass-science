# Supported file formats

Both formats are parsed and validated in C++ (`src/core/io/`). The frontend only
reads the file as text and sends it to the engine, then renders what the
engine returns.

## ASCII STL (`stl_parser.cpp`)

```
solid <optional name>
  facet normal nx ny nz
    outer loop
      vertex x y z
      vertex x y z
      vertex x y z
    endloop
  endfacet
  ...
endsolid <optional name>
```

- Tokens may be separated by any whitespace (one facet per line or one token
  per line). Keywords are case-insensitive.
- Coordinates: decimal or exponent notation (`1.5e-3`). Units are not stored
  in STL; the UI labels them (mm by default).
- The stored facet normal must be finite but is **recomputed** from the vertex
  order, because exported normals are often missing or wrong. Zero-area
  triangles are kept and counted as *degenerate*.

| Input | Result |
|---|---|
| 0-byte file | "The file is empty (0 bytes)…" |
| Only whitespace | "The file contains only whitespace…" |
| Binary STL (size = 84 + 50·n, or control bytes in the first 1 KB) | "This looks like a binary STL file… re-export… ASCII" |
| Does not start with `solid` | "File does not start with the keyword 'solid'…" |
| `solid … endsolid` with no facets | "The STL solid contains no facets…" |
| `nan`, `inf`, `1e999`, `12abc` as a coordinate | "Line N: Invalid vertex coordinate value '…'. … finite …" |
| 2 or 4 vertices in a loop | "Line N: Facet has fewer/more than three vertices…" |
| Misspelt keyword | "Line N: Expected 'loop' but found 'lop'." |
| File ends early | "Line N: Unexpected end of file… may be truncated." |
| > 64 MB or > 1,000,000 triangles | limit message (HTTP 413 above 65 MB) |

## Nodal temperature CSV (`nodal_csv_parser.cpp`)

```
node_id,x_m,y_m,z_m,temperature_C
1,0.000000,0.000000,0.000000,52.1000
...
```

| Column | Type | Unit | Rule |
|---|---|---|---|
| `node_id` | integer | – | > 0, unique |
| `x_m`, `y_m`, `z_m` | real | m | finite |
| `temperature_C` | real | °C | finite, ≥ −273.15 |

- The header must match exactly, in this order. Whitespace around names is
  ignored.
- A UTF-8 byte-order mark, CRLF line endings and blank lines are accepted.
  Blank lines are counted and reported.

| Input | Result |
|---|---|
| 0-byte file / only blank lines | "The file is empty…" / "…only blank lines…" |
| Header only | "The file has a valid header but no data rows…" |
| Wrong header | "Line 1: Header must be exactly 'node_id,x_m,y_m,z_m,temperature_C'…" |
| Wrong number of fields | "Line N: Expected 5 comma-separated values but found 4…" |
| `NaN`, `inf`, `abc` | "Line N: Column 'y_m' has invalid value 'inf'…" |
| Duplicate id | "Line N: Duplicate node_id 1…" |
| `node_id` 0, negative or non-integer | "Line N: node_id '-3' must be a positive integer." |
| Temperature < −273.15 °C | "Line N: … below absolute zero. Check … degrees Celsius." |
| NUL bytes | "The file contains binary data…" |
| > 64 MB or > 2,000,000 nodes | limit message |

## Tested file-size limits

The limit is **64 MB per file** for both formats; the HTTP layer accepts up to
65 MB and answers HTTP 413 above that. Measured on the reference machine
(4 vCPU Intel Xeon 2.8 GHz VM, GCC 13 `-O3`), with synthetic files
generated for the test:

| File | Size | Items | C++ parse | Request round-trip | Outcome |
|---|---|---|---|---|---|
| STL | 1.5 MB | 10,000 triangles | 20 ms | 0.03 s | accepted |
| STL | 15.2 MB | 100,000 triangles | 296 ms | 0.56 s | accepted |
| STL | 61.7 MB | 400,000 triangles | 1.31 s | 2.6 s | accepted |
| STL | 72.5 MB | 470,000 triangles | – | 0.04 s | rejected: too large (HTTP 413) |
| CSV | 0.4 MB | 10,000 nodes | 13 ms | 0.02 s | accepted |
| CSV | 4.1 MB | 100,000 nodes | 87 ms | 0.11 s | accepted |
| CSV | 41.9 MB | 1,000,000 nodes | 805 ms | 1.3 s | accepted |
| CSV | 59.1 MB | 1,400,000 nodes | 2.39 s | 4.1 s | accepted |

What these numbers mean for the full UI:

- **Practical ceilings:** an ASCII STL of 64 MB holds about 400,000 triangles; a 64 MB CSV holds about 1.5 million nodes.
- **400,000-triangle STL:** opened end-to-end in headless Chromium with a software WebGL renderer in about 19 s. Feature edges are skipped automatically above 150,000 triangles.
- **1,000,000-node CSV:** parsed and transferred, but drawing it with the software renderer was too slow to be usable. On a machine with a GPU, expect much faster drawing, but this was not measured.
- **Bundled examples:** 96 triangles and 8,721 nodes, loaded in milliseconds.
