# Engineering Workbench

A small engineering workbench with one interface and a C++17 engine. It:

- imports ASCII STL geometry and nodal temperature results (CSV);
- solves a 1-D thermal finite-element problem and verifies it against the exact solution;
- times serial and multi-threaded batches of independent solves;
- exercises simulated microcontroller, FPGA and JTAG adapters;
- checks a hand-derived backpropagation step numerically.

All numerical work, file parsing and concurrency are in C++. The interface is plain HTML/CSS/JavaScript, served locally by the C++ program and shown in its own window.

> Submission for the PASS Science Works R&D Internship 2026–27 common assignment.
> Candidate: *[name / candidate ID]* · Actual effort: *[hours — fill in]*

---

## 1. Run it (Windows 11 x64, one click)

1. Extract `CandidateID_Workbench.zip` anywhere (Desktop, Downloads, a USB stick).
2. Double-click **`Launch.exe`**.

The workbench opens in its own window: Microsoft Edge in app mode, which ships with Windows 11, or the default browser if Edge is missing. It needs no installation, administrator rights, internet connection, PowerShell or build step. `Launch.exe` is statically linked and depends only on DLLs that ship with Windows (`KERNEL32`, `USER32`, `SHELL32`, `WS2_32`, `msvcrt`).

- **Stop:** press **Quit** (top right). The engine also exits by itself about 3 minutes after the window is closed.
- **Logs:** `logs/workbench.log` next to `Launch.exe`. If that folder is read-only, the log goes to `%TEMP%\EngineeringWorkbench\`.
- **Port:** the engine listens on `127.0.0.1` only (loopback, never on the network). It tries port 8765 and falls back to any free port.
- **Windows SmartScreen:** an unsigned executable can trigger "Windows protected your PC". Choose *More info → Run anyway*. This is a code-signing notice, not an installation step.

### Editor run configuration (VS Code or Antigravity)

1. Open the extracted folder in VS Code or Antigravity (Antigravity is VS Code-based, so it uses the same files).
2. Install the recommended **C/C++** extension (`ms-vscode.cpptools`). It provides the `cppvsdbg` launcher, and the editor offers it automatically from `.vscode/extensions.json`.
3. In *Run and Debug*, select **"Workbench: launch packaged app (Launch.exe)"** and press **F5** (or Ctrl+F5).

**Without the extension:** *Terminal → Run Task… → "Workbench: run packaged app"* (`.vscode/tasks.json`) starts the same `Launch.exe`.

The standalone launcher never needs the editor or internet access.

---

## 2. What is in the package

| Path | Contents |
|---|---|
| `Launch.exe` | Packaged application (C++ engine + local HTTP transport) |
| `app/web/` | Frontend served by `Launch.exe` (HTML/CSS/JS, vendored three.js) and the bundled examples |
| `tools/wb_tests.exe` | Unit tests (run from the package root) |
| `tools/wb_cli.exe` | Regenerates `results/` and `data/` |
| `src/core/` | C++ core library: FEA, parsers, worker pool, device mocks, backprop, CSV export |
| `src/server/` | C++ transport: HTTP routes, JSON mapping, job manager, launcher |
| `src/tools/` | C++ command-line tool and SVG plot writer |
| `src/web/` | Frontend source |
| `tests/` | Unit tests and valid/malformed fixture files |
| `data/` | Example STL and **synthetic** nodal CSV, with `PROVENANCE.md` |
| `results/` | Raw CSV and labelled SVG plots for FEA, performance and backprop |
| `docs/` | Report, file formats, embedded design, AI-use log, ownership, walkthrough |
| `version_control/` | Git bundle and readable commit log |
| `CMakeLists.txt`, `cmake/` | Source build |
| `third_party/` | Vendored header-only dependencies with licences |
| `.vscode/` | Run configuration and tasks |

---

## 3. Architecture

```
┌──────────────────────── Launch.exe (one process) ───────────────────────┐
│  src/server  — transport and presentation glue                          │
│    main.cpp         launcher: port, Edge --app window, idle/quit watchdog│
│    app_server.cpp   REST routes → core calls; exceptions → JSON errors   │
│    json_mapping.cpp core types ↔ JSON (core has no JSON dependency)      │
│    job_manager.cpp  long jobs off the HTTP threads, progress, cancel     │
│                                ▲ calls                                  │
│  src/core  — C++17 library, no transport or UI code                     │
│    fea/       thermal_rod   assembly, Dirichlet elimination, Thomas     │
│    io/        stl_parser, nodal_csv_parser, text_utils                  │
│    parallel/  worker_pool (bounded), batch (generator, serial/parallel, │
│               benchmark)                                                │
│    hardware/  IDeviceAdapter, mock MCU/FPGA/JTAG, DeviceManager (retry) │
│    ml/        backprop (hand-derived gradients, finite differences)     │
│    report/    CSV writers shared by the UI export and the CLI           │
│    system/    CPU / cores / RAM / compiler information                  │
└─────────────────────────────────────────────────────────────────────────┘
          ▲ HTTP on 127.0.0.1 (JSON; geometry as base64 float arrays)
┌─────────────────────── src/web (window) ────────────────────────────────┐
│  main.js router · api.js · chart.js (SVG plots) · viewer3d.js (three.js)│
│  views/: geometry · thermal · performance · hardware · learning         │
└─────────────────────────────────────────────────────────────────────────┘
```

- **Responsiveness:** HTTP requests run on a server thread pool. The benchmark runs as a background job that the UI polls every 250 ms. Cancelling sets an atomic flag that the solver loop checks between cases.
- **Safe cancellation:** a job's results are published only on successful completion, so a cancelled or failed run never replaces the previous results.
- **Errors:** every handler maps core exceptions to user-facing JSON errors:

  | Status | Meaning |
  |---|---|
  | 422 | Parse error (with line number) |
  | 400 | Invalid parameter |
  | 409 | Device in the wrong state |
  | 504 | Device timeout |

  The UI shows the error in a toast and in the status bar.

---

## 4. Supported formats

Details, limits and the measured parse times are in [`docs/FILE_FORMATS.md`](docs/FILE_FORMATS.md).

| Format | Accepted | Rejected with an explanation |
|---|---|---|
| ASCII STL | `solid … facet normal … outer loop … vertex ×3 … endloop endfacet … endsolid`; any whitespace; case-insensitive keywords; up to 64 MB / 1,000,000 triangles | Empty file, whitespace only, binary STL, no facets, NaN/Inf/non-numeric coordinates, wrong vertex count, unexpected tokens, truncated file |
| Nodal CSV | Header exactly `node_id,x_m,y_m,z_m,temperature_C`; UTF-8 BOM, CRLF and blank lines tolerated; up to 64 MB / 2,000,000 nodes | Empty file, header only, wrong header, wrong field count, non-numeric/NaN/Inf values, duplicate or non-positive `node_id`, temperature below −273.15 °C, binary content |

Units: STL has none; the bundled example is in millimetres, and the UI lets you label the axes. CSV coordinates are metres and temperatures are °C. Mesh-to-result registration is not performed.

---

## 5. Build and test from source

Needs CMake ≥ 3.16 and a C++17 compiler. Third-party code is vendored in `third_party/`, so the build downloads nothing.

**Windows (Visual Studio 2022 or Build Tools, "Desktop development with C++"):** from a *Developer PowerShell*:
```bash
cmake -S . -B build
```
```bash
cmake --build build --config Release
```
```bash
ctest --test-dir build -C Release --output-on-failure
```
The app is at `build\package\Launch.exe`.

**Windows (MSYS2 MinGW-w64)** or **Linux/macOS:**
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```
```bash
cmake --build build
```
```bash
ctest --test-dir build --output-on-failure
```
On Linux/macOS the app is `build/package/workbench`. Add `--no-browser` to print the URL instead of opening a window.

**Package the Windows ZIP from Linux** (MinGW-w64 cross-compiler; Wine is optional and runs the tests first):
```bash
CANDIDATE_ID=YourID scripts/package_windows.sh
```

**Regenerate the evidence** (from the project root):
```bash
build/wb_cli all --out results --data data
```

**Run the tests:**
```bash
build/wb_tests
```
or, from the package root on Windows:
```bash
tools\wb_tests.exe
```

**Screenshot walkthrough** (needs Node.js + Playwright, with the app running):
```bash
node scripts/ui_walkthrough.js http://127.0.0.1:8765/ docs/walkthrough
```

### Tests (46)

| Area | Positive | Negative | Numerical |
|---|---|---|---|
| FEA | defaults, boundary values, 2-element hand calculation | invalid L, A, k, N, NaN/Inf, below absolute zero | nodal exactness, midpoint error = qh²/(8k), observed order 2, q = 0 exact, N = 200,000 stability |
| STL | tetrahedron geometry and area, example file, compact layout | empty, whitespace, no facets, binary, NaN, Inf, missing/extra vertex, bad token, truncation | surface area |
| CSV | valid, BOM/CRLF/blank lines, example file | empty, header-only, bad header, missing column, NaN, Inf, duplicate id, non-numeric, below absolute zero, negative id, binary | min/max statistics |
| Parallel | all tasks run, pool bound respected, exceptions surface | invalid batch configuration, cancellation | serial = parallel within 1e-8 °C, deterministic order, seed reproducibility pinned across compilers |
| Hardware | discovery, connect/telemetry/disconnect for all three | unknown id, telemetry while disconnected | single timeout recovered by retry, persistent timeout exhausts retries, recovery after clearing |
| Backprop | forward values | ε ≤ 0 rejected | analytic vs central difference ≤ 1e-5, simultaneous update, monotone loss |

---

## 6. Results at a glance

These figures are from `results/`, regenerated by `wb_cli`. The plots are SVG files in the same folder.

- **FEA:**
  - Nodal error is ≤ 1.2 × 10⁻¹² °C on every mesh.
  - Maximum midpoint error is 1.25, 0.3125, 0.078125 and 0.01953 °C for N = 10, 20, 40, 80.
  - Observed order is 2.000. These values match q h²/(8k) exactly.
- **Backprop:**
  - All four gradients agree with central differences to ≤ 1.2 × 10⁻¹⁰ (tolerance 10⁻⁵).
  - Loss is 1.1825 → 0.6858 after the handwritten update, then 1.1 × 10⁻²⁰ after 100 more steps.
- **Multicore:**
  - Speedup at p = 4 ranged from 1.6× to 2.7× across repeated runs on a shared 4-vCPU cloud VM. Serial and parallel results are bit-identical (max |ΔT| = 0).
  - See [`docs/REPORT.md`](docs/REPORT.md) §3 for the analysis, and run the benchmark on your own machine from the Performance view.

---

## 7. Known limitations

- **Formats:** only ASCII STL; binary STL is detected and rejected. Native ANSYS or CAD formats are out of scope. The CSV example is **synthetic**.
- **Geometry display:**
  - STL normals are recomputed from the vertex winding, and the file's stored normals are ignored.
  - Meshes are drawn as triangle soup; edges are not welded or shared.
  - Feature edges are off by default above 150,000 triangles.
- **Rendering speed:** very large point clouds (≳ 1 M nodes) depend on the GPU. They were only checked with a software renderer, where they parse in < 1 s but draw slowly.
- **Benchmark:** each solve is memory-bound, so speedup is limited by memory bandwidth. Timings on shared or virtualised CPUs are noisy, which is why every run is recorded.
- **Hardware:** all three devices are **simulated**. No USB, serial or JTAG I/O is performed. The pin and voltage tables are design assumptions for named board families, not verified wiring.
- **Packaging:** `Launch.exe` is unsigned, so SmartScreen may ask for confirmation. It was built with MinGW-w64 and tested under Wine; it should also be tested on a physical Windows 11 PC before relying on it.
- **Shutdown:** the engine stops 3 minutes after the last UI contact. Browsers throttle background timers, so a shorter timeout could close a minimised window's engine.

## 8. Documents

- [`docs/REPORT.md`](docs/REPORT.md) — report: design, FEA verification, performance, embedded design, tests, limitations
- [`docs/FILE_FORMATS.md`](docs/FILE_FORMATS.md) — schemas, units, limits, measured parse times
- [`docs/EMBEDDED_DESIGN.md`](docs/EMBEDDED_DESIGN.md) — block diagram, interface/pin-voltage table, integration plan
- [`docs/AI_USE_LOG.md`](docs/AI_USE_LOG.md) — AI assistance disclosure
- [`docs/OWNERSHIP.md`](docs/OWNERSHIP.md) — reused vs original work
- [`docs/walkthrough/`](docs/walkthrough/) — 17 timestamped screenshots and log, captured from `Launch.exe`
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) — dependencies, versions, licences
- `docs/handwritten/` — handwritten backpropagation (scans)
