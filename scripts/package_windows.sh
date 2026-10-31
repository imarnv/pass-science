#!/usr/bin/env bash
# Builds the Windows x64 package on Linux with MinGW-w64 and zips it.
#
#   CANDIDATE_ID=AB1234 scripts/package_windows.sh
#
# Output: dist/<CANDIDATE_ID>_Workbench.zip containing one folder with
# Launch.exe at its root. This script only builds and copies files; all
# application logic lives in the C++ sources.
#
# Requirements: cmake >= 3.16, ninja, g++-mingw-w64-x86-64-posix, zip, git.
# Optional: wine64 (runs the Windows unit tests before packaging).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ID="${CANDIDATE_ID:-CandidateID}"
NAME="${ID}_Workbench"
BUILD="$ROOT/build-win"
STAGE="$ROOT/dist/$NAME"

cd "$ROOT"
echo "==> Cross-compiling (Release, static runtime)"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DWB_STATIC_RUNTIME=ON >/dev/null
cmake --build "$BUILD"

if command -v /usr/lib/wine/wine64 >/dev/null 2>&1 || command -v wine64 >/dev/null 2>&1; then
  echo "==> Running Windows unit tests under Wine"
  WINE="$(command -v wine64 || echo /usr/lib/wine/wine64)"
  WINEDEBUG=-all "$WINE" "$BUILD/wb_tests.exe" | tail -1
fi

echo "==> Staging $STAGE"
rm -rf "$STAGE" "$ROOT/dist/$NAME.zip"
mkdir -p "$STAGE/tools" "$STAGE/version_control"
cp "$BUILD/package/Launch.exe" "$STAGE/"
cp -r "$BUILD/package/app" "$STAGE/"
cp "$BUILD/wb_tests.exe" "$BUILD/wb_cli.exe" "$STAGE/tools/"
cp -r src tests data results docs scripts cmake third_party .vscode "$STAGE/"
cp CMakeLists.txt README.md THIRD_PARTY_NOTICES.md .gitignore "$STAGE/"

echo "==> Version-control evidence"
git bundle create "$STAGE/version_control/workbench.bundle" HEAD main >/dev/null 2>&1
git log --date=iso --stat > "$STAGE/version_control/commit_log.txt"
git log --date=short --pretty=format:'%h %ad %an  %s' > "$STAGE/version_control/commit_log_oneline.txt"

echo "==> Zipping"
(cd "$ROOT/dist" && zip -qr -9 "$NAME.zip" "$NAME")
ls -lh "$ROOT/dist/$NAME.zip"
