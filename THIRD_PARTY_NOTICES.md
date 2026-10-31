# Third-party components

All dependencies are vendored (copied into the repository) at fixed versions,
so builds are reproducible offline. No dependency is downloaded at build or run time.

| Component | Version | Used for | Location | Licence | Source |
|---|---|---|---|---|---|
| cpp-httplib | v0.18.3 | Local HTTP server in the C++ transport layer | `third_party/httplib/httplib.h` | MIT (`third_party/httplib/LICENSE`) | https://github.com/yhirose/cpp-httplib |
| nlohmann/json | v3.11.3 | JSON mapping in the C++ transport layer (not used by the core) | `third_party/nlohmann/json.hpp` | MIT (`third_party/nlohmann/LICENSE.MIT`) | https://github.com/nlohmann/json |
| three.js | r160 (npm `three@0.160.0`) | WebGL rendering of STL meshes and point clouds; `OrbitControls` | `src/web/vendor/three/` | MIT (`src/web/vendor/three/LICENSE`) | https://github.com/mrdoob/three.js |

## Toolchain and runtime

| Item | Version | Notes |
|---|---|---|
| C++ standard | C++17 | `CMAKE_CXX_STANDARD 17`, no compiler extensions |
| CMake | ≥ 3.16 (built with 3.28.3) | |
| Windows package compiler | MinGW-w64 GCC 13 (POSIX threads) | `-O3 -DNDEBUG`, statically linked `libstdc++`, `libgcc` and `winpthread` (GCC Runtime Library Exception permits static linking and redistribution) |
| Linux reference compiler | GCC 13.3.0 | used for `results/` |
| Windows runtime DLLs | system only | KERNEL32, USER32, SHELL32, WS2_32, msvcrt (part of Windows) |
| UI host | Microsoft Edge (`--app` mode) or default browser | ships with Windows 11; not redistributed |

## Development-only tools (not shipped, not needed to run)

| Tool | Purpose |
|---|---|
| Playwright + Chromium | `scripts/ui_walkthrough.js` screenshot walkthrough |
| Wine 9.0 | running the Windows build and tests on Linux before packaging |

Fonts: the UI uses the operating system's font stack (Segoe UI on Windows); no font files are bundled.
