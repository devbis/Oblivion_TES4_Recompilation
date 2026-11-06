@echo off
rem Windows build for the Oblivion Android port. Uses ONLY the JDK in the local .jdk folder
rem (downloaded automatically if missing); system Java / JAVA_HOME are ignored.
rem Usage: build_windows.bat C:\path\to\OblivionFolder
rem Needs: Python 3.10+, Android SDK (platform 34, build-tools 34, cmake 3.22.1) with ANDROID_HOME
rem set, and NDK 29.0.14206865 (or edit NDKV). OblivionFolder holds Oblivion.exe, d3dx9_27.dll
rem and binkw32.dll.
setlocal enabledelayedexpansion
set NDKV=29.0.14206865
if "%~1"=="" (echo usage: build_windows.bat OblivionFolder & exit /b 1)
cd /d "%~dp0"

set "JAVA_HOME="
call :findjdk
if not defined JAVA_HOME (
  echo No JDK in .jdk - downloading portable JDK 17 ^(Temurin, about 180 MB^)...
  if not exist .jdk mkdir .jdk
  powershell -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing 'https://api.adoptium.net/v3/binary/latest/17/ga/windows/x64/jdk/hotspot/normal/eclipse' -OutFile .jdk\jdk17.zip; Expand-Archive -Force .jdk\jdk17.zip .jdk; Remove-Item .jdk\jdk17.zip"
  call :findjdk
)
if not defined JAVA_HOME (
  echo Could not find or download a JDK in .jdk. Put a JDK 17 folder ^(containing bin\java.exe^) inside .jdk and retry.
  exit /b 1
)
set "PATH=%JAVA_HOME%\bin;%PATH%"
echo Using JAVA_HOME=%JAVA_HOME%
"%JAVA_HOME%\bin\java.exe" -version

python -m pip install capstone==5.0.9 pefile==2024.8.26 || exit /b 1
if not exist gen\exe_table.c (
  python tools\regen_gen.py "%~1" || exit /b 1
) else (echo gen\ exists, skipping recompile step. Delete gen\ to redo it.)
python tools\check_imports.py "%~1\Oblivion.exe" "%~1\d3dx9_27.dll" "%~1\binkw32.dll"
cd android
call gradlew.bat --stop
call gradlew.bat assembleDebug "-PndkVersion=%NDKV%" "-Dorg.gradle.java.home=%JAVA_HOME%" || exit /b 1
set "APK=%cd%\app\build\outputs\apk\debug\app-debug.apk"
if not exist "%APK%" (echo Build finished but the APK was not found at %APK% & exit /b 1)
move /Y "%APK%" "%~dp0Oblivion-debug.apk" >nul || (echo Could not move the APK, it is at %APK% & exit /b 1)
echo.
echo APK: %~dp0Oblivion-debug.apk
exit /b 0

:findjdk
rem any folder under .jdk (at most two levels deep) that contains bin\java.exe
for /d %%A in (".jdk\*") do (
  if exist "%%~fA\bin\java.exe" if not defined JAVA_HOME set "JAVA_HOME=%%~fA"
  for /d %%B in ("%%~fA\*") do if exist "%%~fB\bin\java.exe" if not defined JAVA_HOME set "JAVA_HOME=%%~fB"
)
exit /b 0
