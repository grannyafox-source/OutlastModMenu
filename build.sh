#!/usr/bin/env bash
# Builds the 64-bit and 32-bit mod with MinGW-w64 and packages dist/.
#
#   ./build.sh            build both architectures + run the unit tests
#   ./build.sh --no-tests skip the host unit tests
#
# Needs: cmake, ninja (or make), x86_64-w64-mingw32-g++ and i686-w64-mingw32-g++
# (Debian/Ubuntu: apt install cmake ninja-build g++-mingw-w64), zip.
set -euo pipefail
cd "$(dirname "$0")"

RUN_TESTS=1
[[ "${1:-}" == "--no-tests" ]] && RUN_TESTS=0

GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)
JOBS=$(nproc 2>/dev/null || echo 4)
VERSION=$(grep -oP 'OMM_VERSION_STRING "\K[^"]+' src/core/common.h)

if [[ $RUN_TESTS == 1 ]]; then
    echo "== Unit tests (host)"
    cmake -S . -B build/host "${GEN[@]}" -DCMAKE_BUILD_TYPE=Debug >/dev/null
    cmake --build build/host -j "$JOBS"
    ./build/host/omm_tests
fi

for arch in x86_64 i686; do
    echo "== Building $arch"
    cmake -S . -B "build/$arch" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="cmake/mingw-w64-$arch.cmake" >/dev/null
    cmake --build "build/$arch" -j "$JOBS"
done

echo "== Packaging"
rm -rf dist
for arch in x86_64 i686; do
    [[ $arch == x86_64 ]] && folder=Win64 || folder=Win32
    out="dist/OutlastModMenu/Binaries/$folder"
    mkdir -p "$out/OutlastModMenu"
    cp "build/$arch/dinput8.dll" "$out/"
    cp "build/$arch/OMMInjector.exe" "$out/"
    mkdir -p "$out/OutlastModMenu/alternative"
    cp "build/$arch/OutlastModMenu.asi" "build/$arch/OutlastModMenu.dll" "$out/OutlastModMenu/alternative/"
    cp -r package/OutlastModMenu/. "$out/OutlastModMenu/"
    mkdir -p "dist/plugin_example/$folder"
    cp "build/$arch/omm_plugin_example.dll" "dist/plugin_example/$folder/"
done
cp README.md dist/OutlastModMenu/README.md
cp -r docs dist/OutlastModMenu/docs
cp -r sdk dist/OutlastModMenu/sdk
cp third_party/imgui/LICENSE.txt dist/OutlastModMenu/docs/LICENSE-imgui.txt
cp third_party/minhook/LICENSE.txt dist/OutlastModMenu/docs/LICENSE-minhook.txt
(cd dist && zip -qr "OutlastModMenu-v$VERSION.zip" OutlastModMenu plugin_example)
echo "Done: dist/OutlastModMenu-v$VERSION.zip"
