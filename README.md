# The Elder Scrolls IV: Oblivion — Android and macOS

An unofficial work-in-progress port of the 2006 PC version of *The Elder Scrolls IV: Oblivion* (not Oblivion Remastered). The project recompiles the original 32-bit Windows executable and selected DLLs into native ARM64 code, then runs them with an SDL2 and DXVK-based runtime.

The Android build targets ARM64 devices with Vulkan 1.1. The desktop build targets Apple Silicon Macs and uses MoltenVK to run the Vulkan renderer on Metal. macOS Intel is not supported.

This repository does not include the game or its assets. You need your own copy of the PC game, version 1.2.0.416. Do not redistribute the game files or generated code.

Discord: <http://discord.gg/df3cK2JCqs>

Gameplay video: <https://www.youtube.com/watch?v=cWujRxtQTP0>

## Game files

The Android launcher requires the selected game folder to contain:

- `Oblivion.exe` (version 1.2.0.416)
- `Data/Oblivion.esm` and the rest of the game's `Data/` directory
- `d3dx9_27.dll` (32-bit, from the DirectX June 2010 redist)
- `binkw32.dll`

The desktop code generator also searches common redistributable and neighboring system-library directories for the two DLLs. The Android launcher checks the executable version before enabling Play.

## Android

### Device requirements

- Android 9 or newer, ARM64 (`arm64-v8a`)
- Vulkan 1.1
- Use a game controller; touch controls are not available in this build.

The port has been tested on a Snapdragon device with an Adreno 610 GPU (Retroid Pocket Duo Lite). Performance and compatibility vary by device.

### Install and play

1. Copy the complete game folder to internal storage or an SD card, outside `Android/data` (for example, `Internal storage/Oblivion`).
2. Install the debug APK and open the launcher.
3. Choose the game folder, adjust the graphics options, and press **Play**. Grant file access if Android asks.

The launcher lets you set aspect ratio, resolution, antialiasing, texture size, shadows, and other graphics options. Saves, configuration, logs, and the DXVK state cache are stored under `<game folder>/port/`.

### Build the Android APK

On the build machine, install Python 3, JDK 17, Android SDK platform 34 and build-tools 34, CMake 3.22.1, and NDK `29.0.14206865`. Set `ANDROID_HOME` to your Android SDK location.

Install the code-generation dependencies and generate native code from your game files:

```sh
python3 -m pip install capstone==5.0.9 pefile==2024.8.26
python3 tools/regen_gen.py "/path/to/Oblivion"
```

Build the APK:

```sh
cd android
./gradlew assembleDebug -PndkVersion=29.0.14206865
```

The APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`. On Windows, `build_windows.bat C:\path\to\Oblivion` runs code generation and builds the APK.

## macOS (Apple Silicon)

The desktop build targets arm64 Macs. Install Xcode Command Line Tools, Python 3, CMake, Ninja, and Vulkan with MoltenVK. You can use the LunarG Vulkan SDK or a Homebrew Vulkan loader and MoltenVK installation.

Install the Python dependencies, then build and launch with the game folder:

```sh
python3 -m pip install capstone==5.0.9 pefile==2024.8.26
./desktop/build.sh "/path/to/Oblivion"
./desktop/run.sh "/path/to/Oblivion"
```

For a GOG Linux installation, set `GAME` to its game-data directory. The code generator searches neighboring system-library directories for the required DLLs:

```sh
GAME="/path/to/Oblivion"
./desktop/build.sh "$GAME"
./desktop/run.sh "$GAME"
```

The build script generates code from the executable and DLLs, then compiles `desktop/build/lb` for Apple Silicon. Code generation takes several minutes. If `gen/` is already present and you only need to rebuild the runtime, run `./desktop/build.sh` without a game-folder argument.

The run script uses `./save` for settings, saves, and the DXVK state cache by default. Pass a second path to use a different save directory. Set `LB_LOG=1` to enable runtime logging.

On Mac, **Command-Return** toggles native fullscreen. The game captures the pointer at launch; **Escape** releases it, and clicking the game window captures it again. The game uses Oblivion's default PC keyboard and mouse bindings.

## Android controller controls

| Button | Action |
| --- | --- |
| Left stick / right stick | Move / look |
| A | Activate |
| B | Journal |
| X | Ready / sheath weapon |
| Y | Jump |
| LB | Grab objects |
| RB | Cast spell |
| LT | Block |
| RT | Attack |
| L3 | Toggle sneak |
| R3 | Change camera |
| D-pad | Hotkeys 1 to 4 |
| Back / Start | Wait / pause |

Hotkeys require the [NorthernUI Hotkeys](https://www.nexusmods.com/oblivion/mods/51534) add-on.

## Known limitations

- This is a work in progress. Some Windows imports are still unimplemented; crashes and incomplete features are possible. On desktop, set `LB_LOG=1` to print runtime diagnostics.
- OBSE plugins are not supported. Compatibility with individual mods may vary.
- Android touch controls and gamepad rumble are not implemented.
- The port requires the original 1.2.0.416 executable; other versions are not supported.

## Legal

The Elder Scrolls, Oblivion, Bethesda, and all related names and assets belong to their respective owners. This project is not affiliated with or endorsed by them. Do not redistribute game files or generated code. Third-party code retains its own licenses; SDL2 and DXVK are included under their respective licenses.
