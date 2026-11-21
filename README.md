# The Elder Scrolls IV: Oblivion (PC, 2006) for Android

An unofficial "native" Android port of the original 2006 PC version of *The Elder Scrolls IV: Oblivion* (not Remastered) from Steam. It should run on any arm64 phone with Vulkan 1.1 and is tested on a Snapdragon device with an Adreno 610 GPU (Retroid Pocket Duo Lite, if anyone is curious).

This repository contains no game code or data. You need your own copy of the PC game, which is used as a basis for the recompilation. Personally, I used the Steam version.

Discord: http://discord.gg/df3cK2JCqs

### Expect crashes, this is an early WIP build!

Youtube link below:

[![Watch the video](https://img.youtube.com/vi/cWujRxtQTP0/hqdefault.jpg)](https://www.youtube.com/watch?v=cWujRxtQTP0)

## Controls (controller)
| Button | Action |
|---|---|
| Left stick / right stick | Move / look |
| A | Activate |
| B | Menu / journal |
| X | Ready weapon |
| Y | Jump |
| LB | Run (hold Shift; walks while Always Run is on) |
| RB | Cast spell |
| LT / RT | Block / attack |
| L3 / R3 | Sneak / change camera |
| D-pad | Hotkeys 1 to 4 |
| Back / Start | Wait / pause |

## Build
**Windows:** run `build_windows.bat C:\path\to\OblivionFolder` (does steps 2-4 below with `tools/regen_gen.py`, no WSL needed; set `ANDROID_HOME` first). Linux/macOS: `python3 tools/regen_gen.py GAME` or `tools/regen_gen.sh GAME`.

1. Put in one folder `GAME`: `Oblivion.exe`, `d3dx9_27.dll` (32-bit, DirectX June 2010 redist), `binkw32.dll`.
2. `pip install capstone==5.0.9 pefile==2024.8.26`, then `tools/regen_gen.sh GAME` (discovery takes minutes, not hours; lifting ~10 min; ~190 MB of C for the exe).
3. `python3 tools/check_imports.py GAME/Oblivion.exe GAME/d3dx9_27.dll GAME/binkw32.dll` - anything listed has no shim.
4. `cd android && ./gradlew assembleDebug -PndkVersion=29.0.14206865` (JDK 17, SDK 34, CMake 3.22.1, NDK r27+).
5. Copy the whole Oblivion folder (with `Data/`, the two DLLs) to the phone and choose it in the app.

## Known gaps
- **Expect crashes.** `port/log.txt` lists imports with no shim and any `rt_unimpl` trap.
- 271 of 48,462 discovered functions were rejected by the lifter (data mis-decoded as code, segment-register/BCD instructions); check `rejected` output of `lift.py`.
- Default key bindings are Oblivion's PC defaults.
- Mouse wheel zoom works only with a physical mouse. No gamepad rumble, no 4 GB patch/OBSE support.

# Instructions

## Game files you need

Everything comes from your own PC install of *The Elder Scrolls IV: Oblivion* (2006).

TODO: List

- Steam Oblivion folder
- d3dx9_27.dll

## Legal
The Elder Scrolls, Oblivion, Bethesda and all related names and assets belong to their respective owners. This project is not affiliated with or endorsed by them. Do not redistribute anything produced from the game or game files. Third-party code keeps its own licenses: SDL2 (zlib) in `android/app/jni/SDL2`, dxvk-native (zlib) in `android/app/jni/dxvk`.
