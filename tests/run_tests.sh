#!/usr/bin/env bash
# Builds and runs the host unit tests with AddressSanitizer/UBSan, in 64-bit
# and (when the multilib toolchain is installed) 32-bit mode, so both pointer
# sizes of the engine layout are covered.
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=(tests/test_main.cpp tests/test_files.cpp tests/stubs.cpp
     src/ue3/scanner.cpp src/ue3/engine.cpp
     src/core/ini.cpp src/core/strutil.cpp src/core/fileutil.cpp src/core/settings.cpp src/core/log.cpp
     src/core/paths.cpp src/core/guard.cpp src/game/initweaks.cpp src/game/modloader.cpp)
OUT=${TMPDIR:-/tmp}/omm_tests
CXX=${CXX:-g++}

echo "== 64-bit (ASan + UBSan)"
$CXX -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$OUT" "${SRC[@]}"
"$OUT"

if echo 'int main(){}' | $CXX -m32 -x c++ - -o "$OUT.probe" 2>/dev/null; then
    echo "== 32-bit"
    $CXX -std=c++17 -O1 -g -m32 -Wall -Wextra -o "$OUT"32 "${SRC[@]}"
    "$OUT"32
else
    echo "(32-bit toolchain not installed - skipping the -m32 run)"
fi
