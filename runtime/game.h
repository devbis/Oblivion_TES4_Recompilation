/* Per-game names: this runtime is shared by the LEGO Batman and LEGO Marvel
 * ports, Skyrim and Oblivion. Build with -DGAME_MARVEL, -DGAME_SKYRIM or
 * -DGAME_OBLIVION (default is Batman). */
#pragma once
#if defined(GAME_OBLIVION)
/* The Elder Scrolls IV: Oblivion 1.2.0.416 (2006, Gamebryo, D3D9, DirectSound,
 * DirectInput keyboard + mouse). Only d3dx9_27 and binkw32 are recompiled
 * besides the exe; audio is DirectSound (runtime/dsound.c), not XAudio2. */
#define GAME_EXE       "Oblivion.exe"
#define DLL_D3DX9      "d3dx9_27.dll"
#define DLL_EXTRA      "binkw32.dll"
#define GAME_WIN_DIR   "C:\\Oblivion"
#define GAME_WIN_PREFIX "\\oblivion\\"
#define GAME_TITLE     "Oblivion"
#define GAME_PKG_DIR   "com.tj.oblivion"
#elif defined(GAME_SKYRIM)
#define GAME_EXE       "TESV.exe"
#define DLL_D3DX9      "d3dx9_42.dll"
#define DLL_EXTRA      "binkw32.dll"
#define GAME_WIN_DIR   "C:\\Skyrim"
#define GAME_WIN_PREFIX "\\skyrim\\"
#define GAME_TITLE     "Skyrim"
#define GAME_PKG_DIR   "com.tj.skyrim"
#elif defined(GAME_MARVEL)
#define GAME_EXE       "LEGOMARVEL.exe"
#define DLL_D3DX9      "d3dx9_43.dll"
#define DLL_EXTRA      "D3DCompiler_43.dll"
#define GAME_WIN_DIR   "C:\\LEGOMarvel"
#define GAME_WIN_PREFIX "\\legomarvel\\"
#define GAME_TITLE     "LEGO Marvel Super Heroes"
#define GAME_PKG_DIR   "com.tj.legomarvel"
#else
#define GAME_EXE       "LEGOBatman.exe"
#define DLL_D3DX9      "d3dx9_35.dll"
#define DLL_EXTRA      "binkw32.dll"
#define GAME_WIN_DIR   "C:\\LEGOBatman"
#define GAME_WIN_PREFIX "\\legobatman\\"
#define GAME_TITLE     "LEGO Batman"
#define GAME_PKG_DIR   "com.tj.legobatman"
#define GAME_BATMAN 1
#endif
