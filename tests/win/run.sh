#!/usr/bin/env bash
# Builds the Windows-only tests (crash guard + end-to-end harness) for 64- and
# 32-bit with MinGW-w64 and runs them with Wine (or natively under MSYS2).
#   tests/win/run.sh            build + run both architectures
# Needs: x86_64-w64-mingw32-g++, i686-w64-mingw32-g++, wine (wine64 + wine32).
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=${OUT:-/tmp/omm_wintests}
mkdir -p "$OUT"

MOD_SRC=$(ls src/core/*.cpp src/ue3/*.cpp src/game/*.cpp src/game/data/*.cpp src/render/*.cpp src/ui/*.cpp \
          src/app/app.cpp src/proxy/dinput8_proxy.cpp)
IMGUI=(third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp
       third_party/imgui/imgui_widgets.cpp third_party/imgui/backends/imgui_impl_dx9.cpp
       third_party/imgui/backends/imgui_impl_dx11.cpp third_party/imgui/backends/imgui_impl_win32.cpp)
MINHOOK=(third_party/minhook/src/buffer.c third_party/minhook/src/hook.c third_party/minhook/src/trampoline.c
         third_party/minhook/src/hde/hde32.c third_party/minhook/src/hde/hde64.c)

WINE64=$(command -v wine64 || echo /usr/lib/wine/wine64)
WINE32=$(command -v wine || echo /usr/lib/wine/wine)
export WINEDEBUG=${WINEDEBUG:--all}
export WINEPREFIX=${WINEPREFIX:-$OUT/wineprefix}

status=0
for arch in x86_64 i686; do
    CXX=$arch-w64-mingw32-g++
    CC=$arch-w64-mingw32-gcc
    dir="$OUT/$arch"
    mkdir -p "$dir/obj"
    echo "== $arch: building"
    # Third-party objects once per architecture.
    for f in "${MINHOOK[@]}"; do
        o="$dir/obj/$(basename "$f").o"
        [[ -f $o ]] || $CC -O2 -c "$f" -Ithird_party/minhook/include -o "$o"
    done
    for f in "${IMGUI[@]}"; do
        o="$dir/obj/$(basename "$f").o"
        [[ -f $o ]] || $CXX -std=c++17 -O2 -c "$f" -Ithird_party/imgui -DD3DCompile=OMM_D3DCompile -o "$o"
    done
    $CXX -std=c++17 -O1 -g -Wall -Wextra -D__USE_MINGW_ANSI_STDIO=1 -DUNICODE -D_UNICODE \
        -Ithird_party/minhook/include -Ithird_party/imgui -Isrc \
        -o "$dir/olgame_test.exe" tests/win/integration_test.cpp $MOD_SRC "$dir"/obj/*.o \
        -static -ldxguid -ldwmapi -lshell32 -luser32 -limm32 -lgdi32 -lole32
    $CXX -std=c++17 -O2 -Wall -D__USE_MINGW_ANSI_STDIO=1 -static -o "$dir/guard_test.exe" \
        tests/win/guard_test.cpp src/core/guard.cpp src/core/log.cpp src/core/strutil.cpp
    [[ $arch == x86_64 ]] && WINE=$WINE64 || WINE=$WINE32
    if [[ ! -d $WINEPREFIX ]]; then "$WINE64" wineboot -i >/dev/null 2>&1 || true; fi
    echo "== $arch: guard test"
    (cd "$dir" && rm -rf OutlastModMenu && timeout 120 "$WINE" guard_test.exe) || status=1
    echo "== $arch: end-to-end harness"
    (cd "$dir" && rm -rf OutlastModMenu && timeout 300 "$WINE" olgame_test.exe) || status=1
done
exit $status
