#!/usr/bin/env python3
"""Cross-platform replacement for regen_gen.sh (Windows, Linux, macOS).
Usage: python tools/regen_gen.py GAME_DIR
GAME_DIR needs Oblivion.exe (1.2.0.416), d3dx9_27.dll and binkw32.dll
(also found in a redist/ subfolder). Needs: pip install capstone==5.0.9 pefile==2024.8.26"""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")


def find(game, name):
    for d in (game, os.path.join(game, "redist"), os.path.join(game, "Redist")):
        if os.path.isdir(d):
            for f in os.listdir(d):
                if f.lower() == name:
                    return os.path.join(d, f)
    sys.exit("missing %s in %s" % (name, game))


def run(*args, cwd=ROOT):
    print(">", " ".join(args), flush=True)
    subprocess.check_call([sys.executable, *args], cwd=cwd, env=dict(os.environ, LIFT_GAME="oblivion"))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    game = os.path.abspath(sys.argv[1])
    exe, d3dx, bink = (find(game, n) for n in ("oblivion.exe", "d3dx9_27.dll", "binkw32.dll"))
    os.makedirs(os.path.join(ROOT, "analysis"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "gen"), exist_ok=True)
    un = os.path.join(ROOT, "analysis", "Oblivion_unpacked.exe")
    run("tools/unpack_oblivion.py", exe, un)
    run("discover.py", un, os.path.join(ROOT, "analysis", "functions.json"), cwd=TOOLS)
    run("discover.py", d3dx, os.path.join(ROOT, "analysis", "functions_d3dx.json"), "08000000", cwd=TOOLS)
    run("discover.py", bink, os.path.join(ROOT, "analysis", "functions_bink.json"), "09000000", cwd=TOOLS)
    run("tools/lift.py", un, "analysis/functions.json", "gen", "exe")
    run("tools/lift.py", d3dx, "analysis/functions_d3dx.json", "gen", "d3dx9", "08000000")
    run("tools/lift.py", bink, "analysis/functions_bink.json", "gen", "bink", "09000000")
    print("gen/ is ready")


main()
