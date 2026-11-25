#!/bin/sh
# Builds the native desktop runtime. On Apple Silicon this produces an arm64
# binary and uses MoltenVK through the installed Vulkan SDK/loader.
# Usage: desktop/build.sh [game data dir]
# First build needs Python packages capstone==5.0.9 and pefile==2024.8.26.
# JOBS: parallel compiles (each generated file needs ~1-2 GB of RAM).
set -e
cd "$(dirname "$0")/.."
JOBS=${JOBS:-3}
PYTHON=${PYTHON:-python3}
if [ "$#" -gt 1 ]; then
    echo "usage: desktop/build.sh [game data dir]" >&2
    exit 2
fi
if [ "$#" -eq 1 ]; then
    "$PYTHON" tools/regen_gen.py "$1"
elif [ ! -d gen ] || [ -z "$(find gen -name '*.c' -print -quit)" ]; then
    echo "generated game code is missing; pass the game data directory:" >&2
    echo "  desktop/build.sh /path/to/game_info/data" >&2
    exit 2
fi

if [ "$(uname -s)" = Darwin ]; then
    cmake -S desktop -B desktop/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES=arm64 -DLB_GEN_JOBS="$JOBS"
else
    cmake -S desktop -B desktop/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLB_GEN_JOBS="$JOBS"
fi
cmake --build desktop/build --parallel "$JOBS"
