# meshoptimizer (not bundled)

Put the library's `src/` folder here so that `third_party/meshoptimizer/src/meshoptimizer.h` exists:

    git clone --depth 1 https://github.com/zeux/meshoptimizer.git /tmp/mo
    cp -r /tmp/mo/src third_party/meshoptimizer/src
    cp /tmp/mo/LICENSE.md third_party/meshoptimizer/

or download a release zip from https://github.com/zeux/meshoptimizer/releases and copy its `src/` folder.
Any 0.18+ release works (the port calls `meshopt_optimizeVertexCache` only). MIT licensed.

The Android CMake build, the desktop `Makefile` and `build_windows.bat` pick it up automatically
(`-DLB_MESHOPT`). Without the folder the port builds exactly as before and the hook is compiled out.
Turn it off at run time with `LB_NOMESHOPT=1` (desktop) or an empty `port/save/nomeshopt.txt` (phone).
