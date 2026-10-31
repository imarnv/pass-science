# Example data: provenance, units and schema

Both files were **self-created** for this project by the generator in
`src/tools/example_data.cpp`, and are regenerated bit-for-bit with:

```bash
build/wb_cli datagen --out data
```

Neither file contains third-party or personal data. Both are released with the project.

## `heatsink.stl` — self-created geometry

| Property | Value |
|---|---|
| Format | ASCII STL |
| Solid name | `finned_heat_sink_mm` |
| Triangles | 96 (8 boxes × 12) |
| Units | **millimetres** (STL itself stores no units) |
| Extent | 40 × 40 × 22 mm |
| Construction | One 40 × 40 × 4 mm base plate plus 7 fins (1.6 mm thick, 18 mm tall), each a closed box. Boxes touch at shared faces. This is a visualisation model, not a boolean union, so it is not a single watertight solid. |
| Normals | Outward unit normals, counter-clockwise vertex order seen from outside |

An "existing CAD file" is simulated by exporting this geometry in the same
ASCII STL layout that CAD tools produce (`solid` / `facet normal` / `outer loop` /
`vertex` / `endloop` / `endfacet` / `endsolid`).

## `synthetic_nodal_temperatures.csv` — SYNTHETIC result data

> **This is not an ANSYS export.** It was generated analytically to exercise the
> importer and viewer with an ANSYS-style nodal CSV layout. The UI labels it
> *SYNTHETIC* when loaded.

| Property | Value |
|---|---|
| Schema | `node_id,x_m,y_m,z_m,temperature_C` (header row, comma-separated, `.` decimal point) |
| Rows | 8,721 nodes, ids 1…8721, unique |
| Coordinate units | metres (the same heat sink as `heatsink.stl`, scaled from mm to m) |
| Temperature units | degrees Celsius |
| Sampling | Points on the fin faces, fin mid-planes and channel mid-planes; 2.5 mm in y, 1 mm in z; only points inside the solid are kept |
| Field | T = 25 + 62·exp(−r²/(2·11²))·exp(−z/14) + 4·exp(−z/3) °C, with r the distance (mm) from the base-centre axis and z the height (mm). This represents a heat source under the base centre cooling up the fins; it is **not** a solution of the heat equation. |
| Range | 25.5 °C … 91.0 °C |

### Exporting real ANSYS results to this schema

In ANSYS Mechanical: select the *Temperature* result → right-click → *Export → Export Text File*.
The text export typically lists node number, X/Y/Z location and temperature, with
units following the project's unit system (check the header of your own export). Then:

1. set the unit system to metres and degrees Celsius before exporting;
2. rename the header to `node_id,x_m,y_m,z_m,temperature_C`;
3. save as comma-separated (not tab-separated) with `.` as the decimal mark.

The importer rejects anything else with a line-numbered message rather than guessing.
No real ANSYS export is included, because no licensed solver was used.
