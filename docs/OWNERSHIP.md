# Ownership statement


## Reused third-party work (unmodified)

| Component | Files | Licence |
|---|---|---|
| cpp-httplib v0.18.3 | `third_party/httplib/` | MIT |
| nlohmann/json v3.11.3 | `third_party/nlohmann/` | MIT |
| three.js r160 (`three.module.min.js`, `OrbitControls.js`) | `src/web/vendor/three/` | MIT |

Board names in `EMBEDDED_DESIGN.md` (STM32 Nucleo-64, Digilent Arty A7, FT2232H) are
used only as design assumptions. No vendor code or documents are included.

## Project work written for this submission

Everything else in the repository was written for this assignment, with AI assistance as disclosed in `AI_USE_LOG.md`:

| Area | Files |
|---|---|
| C++ core | `src/core/**` |
| C++ transport and launcher | `src/server/**` |
| C++ tools | `src/tools/**` |
| Frontend | `src/web/index.html`, `src/web/css/`, `src/web/js/`, `src/web/assets/` |
| Tests and fixtures | `tests/**` |
| Build and packaging | `CMakeLists.txt`, `cmake/`, `scripts/`, `.vscode/` |
| Example data | `data/` (self-created; the CSV is synthetic) |
| Documentation | `README.md`, `docs/**` |

The handwritten derivation in `docs/handwritten/` is the candidate's own work.

Signed: **Arnav Mehta** (Reg. no. RA2311056010137)   Date: **1 November 2026**
