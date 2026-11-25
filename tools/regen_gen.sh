#!/bin/sh
# Regenerates gen/ (recompiled C) from your own Oblivion files.
# Usage: tools/regen_gen.sh GAME_DIR
# GAME_DIR must contain Oblivion.exe (1.2.0.416, Steam or not), d3dx9_27.dll
# (DirectX June 2010 redistributable, 32-bit) and binkw32.dll (game folder).
# The Steam wrapper is removed automatically (tools/unpack_oblivion.py).
# Needs Python 3.10+: pip install capstone==5.0.9 pefile==2024.8.26
set -e
G=$(cd "${1:?usage: tools/regen_gen.sh GAME_DIR}" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
find_file() { for f in "$G"/* "$G"/redist/* "$G"/Redist/* "$G"/../dlls/syswow64/* "$G"/../dlls/system32/*; do
    [ "$(echo "${f##*/}" | tr A-Z a-z)" = "$1" ] && { echo "$f"; return; }; done
    echo "missing $1 in $G" >&2; exit 1; }
SRC=$(find_file oblivion.exe); D3DX=$(find_file d3dx9_27.dll); BINK=$(find_file binkw32.dll)
mkdir -p analysis gen
EXE=analysis/Oblivion_unpacked.exe
python3 tools/unpack_oblivion.py "$SRC" "$EXE"
export LIFT_GAME=oblivion
(cd tools && python3 discover.py "../$EXE" ../analysis/functions.json)
(cd tools && python3 discover.py "$D3DX" ../analysis/functions_d3dx.json 08000000)
(cd tools && python3 discover.py "$BINK" ../analysis/functions_bink.json 09000000)
python3 tools/lift.py "$EXE" analysis/functions.json gen exe
python3 tools/lift.py "$D3DX" analysis/functions_d3dx.json gen d3dx9 08000000
python3 tools/lift.py "$BINK" analysis/functions_bink.json gen bink 09000000
echo "gen/ is ready"
