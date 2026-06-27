@echo off
REM ===========================================================================
REM  build.bat -- one-shot Windows build of ai-reader (Qt6) that SELF-SETS-UP
REM  the toolchain, so plain "build.bat" just works with no manual env steps.
REM
REM  Usage:
REM      build.bat                 Release, incremental (default)
REM      build.bat debug           Debug build
REM      build.bat clean           wipe build\ first, then Release
REM      build.bat release clean   wipe build\ first, then Release
REM      build.bat debug clean     wipe build\ first, then Debug
REM
REM  Toolchain auto-setup (in order); if any is missing it says exactly which
REM  one and where to get it, then exits -- it never runs a broken cmake:
REM    1. MSVC env -- if cl.exe is not on PATH, locate Visual Studio via
REM       vswhere and CALL vcvars64.bat (falls back to common VS2022/2019 paths).
REM    2. cmake -- PATH, then %CMAKE_HOME%\bin, then %QT_HOME%\Tools\CMake_64\bin,
REM       then the QT_PREFIX-derived %QT_TOOLS%\CMake_64\bin.
REM    3. ninja -- PATH, then %QT_HOME%\Tools\Ninja, then %QT_TOOLS%\Ninja.
REM       ai-reader uses the Ninja generator on every platform.
REM    4. git -- required: the CMake configure FetchContent's cmark-gfm,
REM       qtkeychain, tinyxml2 and MicroTeX, which need git to clone.
REM
REM  Qt kit (QT_PREFIX) resolution: an explicit QT_PREFIX wins; else the newest
REM  6.x msvc*_64 kit found under %QT_HOME%; else the same under C:\Qt. The kit
REM  MUST include the "Qt PDF" component (Qt6Pdf / Qt6PdfQuick) -- ai-reader
REM  hard-requires it and cmake configure fails clearly if it is absent.
REM
REM  Unlike a Qt5 qt5_add_resources project, this does NOT wipe build\ each run:
REM  Qt6 qmlcachegen tracks QML changes incrementally, and a clean wipe would
REM  re-download and rebuild every FetchContent dep (slow). Pass "clean" to
REM  force a from-scratch build.
REM
REM  ASCII-only on purpose: cmd.exe parses .bat in the system codepage (GBK on
REM  Chinese Windows), not UTF-8, so a non-ASCII byte can break the parser. No
REM  parenthesised if/else blocks and no '>' inside any echo; errors use goto.
REM ===========================================================================

setlocal

REM --- Args: build type (release default) + optional "clean" -----------------
set "CFG=%~1"
set "ARG2=%~2"
set "DOCLEAN="
if /I "%CFG%"=="clean" set "DOCLEAN=1"
if /I "%CFG%"=="clean" set "CFG="
if /I "%ARG2%"=="clean" set "DOCLEAN=1"
if "%CFG%"=="" set "CFG=release"
if /I "%CFG%"=="release" set "BUILD_TYPE=Release"
if /I "%CFG%"=="debug"   set "BUILD_TYPE=Debug"
if not defined BUILD_TYPE goto :bad_cfg

REM --- Resolve the Qt6 kit (QT_PREFIX) ---------------------------------------
REM Priority: (1) QT_PREFIX set wins; (2) a 6.x msvc*_64 kit under %QT_HOME%;
REM (3) the same under C:\Qt. If auto-detect picks the wrong one (multiple Qt
REM versions installed), set QT_PREFIX explicitly to override.
if defined QT_PREFIX goto :qt_prefix_done
if defined QT_HOME for /d %%V in ("%QT_HOME%\6.*") do for /d %%K in ("%%V\msvc*_64") do if exist "%%K\bin\qmake.exe" set "QT_PREFIX=%%K"
if defined QT_PREFIX goto :qt_prefix_done
for /d %%V in ("C:\Qt\6.*") do for /d %%K in ("%%V\msvc*_64") do if exist "%%K\bin\qmake.exe" set "QT_PREFIX=%%K"
:qt_prefix_done
if not defined QT_PREFIX goto :no_qt

REM --- Derive the Qt Tools dir from QT_PREFIX --------------------------------
REM QT_PREFIX = ...\Qt\6.x.y\msvc2022_64  ->  Qt root = ...\Qt  ->  ...\Qt\Tools
set "QT_TOOLS="
for %%I in ("%QT_PREFIX%\..\..") do set "QT_ROOT=%%~fI"
if defined QT_ROOT set "QT_TOOLS=%QT_ROOT%\Tools"

echo.
echo ===========================================================================
echo  Building ai-reader (Qt6)
echo  Build type:  %BUILD_TYPE%
echo  Qt kit:      %QT_PREFIX%
echo ===========================================================================
echo.

REM === 1. MSVC environment (cl / link) ======================================
where cl >nul 2>nul && goto :have_msvc
echo [build] Locating Visual Studio (MSVC) ...

set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -property installationPath`) do set "VSINSTALL=%%I"
if defined VSINSTALL if exist "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"

REM Fallback to common fixed install paths (VS2022 in Program Files, VS2019 in
REM Program Files (x86); Community/Professional/Enterprise/BuildTools).
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"      set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"   set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"     set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"      set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"    set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat"   set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"   set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not defined VCVARS goto :no_msvc
echo [build] Initializing MSVC env: "%VCVARS%"
call "%VCVARS%" >nul
where cl >nul 2>nul && goto :have_msvc
goto :no_msvc

:have_msvc
echo [build] MSVC compiler cl.exe ready.

REM === 2. cmake =============================================================
where cmake >nul 2>nul && goto :have_cmake
if defined CMAKE_HOME if exist "%CMAKE_HOME%\bin\cmake.exe" set "PATH=%CMAKE_HOME%\bin;%PATH%"
where cmake >nul 2>nul && goto :have_cmake
if defined QT_HOME if exist "%QT_HOME%\Tools\CMake_64\bin\cmake.exe" set "PATH=%QT_HOME%\Tools\CMake_64\bin;%PATH%"
where cmake >nul 2>nul && goto :have_cmake
if defined QT_TOOLS if exist "%QT_TOOLS%\CMake_64\bin\cmake.exe" set "PATH=%QT_TOOLS%\CMake_64\bin;%PATH%"
where cmake >nul 2>nul && goto :have_cmake
goto :no_cmake

:have_cmake
echo [build] cmake ready.

REM === 3. ninja ============================================================
where ninja >nul 2>nul && goto :have_ninja
if defined QT_HOME if exist "%QT_HOME%\Tools\Ninja\ninja.exe" set "PATH=%QT_HOME%\Tools\Ninja;%PATH%"
where ninja >nul 2>nul && goto :have_ninja
if defined QT_TOOLS if exist "%QT_TOOLS%\Ninja\ninja.exe" set "PATH=%QT_TOOLS%\Ninja;%PATH%"
where ninja >nul 2>nul && goto :have_ninja
goto :no_ninja

:have_ninja
echo [build] ninja ready.

REM === 4. git (the CMake configure FetchContent's the deps) ================
where git >nul 2>nul && goto :have_git
goto :no_git

:have_git
echo [build] git ready.

REM --- Optional clean (default is incremental; deps are expensive to rebuild) -
if defined DOCLEAN if exist build echo [build] clean: removing stale build directory...
if defined DOCLEAN if exist build rmdir /S /Q build

REM --- Configure ------------------------------------------------------------
REM CMAKE_POLICY_VERSION_MINIMUM=3.5 is REQUIRED: cmake 4.x (bundled with newer
REM Qt) rejects the pre-3.5 cmake_minimum_required in FetchContent deps like
REM cmark-gfm. Harmless on cmake 3.x.
echo [build] Configuring (%BUILD_TYPE%) ...
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_PREFIX_PATH="%QT_PREFIX%" -DCMAKE_POLICY_VERSION_MINIMUM=3.5
if errorlevel 1 goto :cfg_fail

echo [build] Compiling...
cmake --build build
if errorlevel 1 goto :build_fail

echo.
echo [build] SUCCESS. Output: build\ai-reader.exe
echo [build] CMake also staged dist\ai-reader.exe + dist\microtex_res.
echo [build] Next, to make a runnable / installable build:
echo     windeploy.bat       stage the Qt runtime + MSVC DLLs into dist\
echo     iscc AiReader.iss   build the Windows installer (needs Inno Setup)
echo.
endlocal
exit /b 0

:no_msvc
echo.
echo [build] ERROR: the MSVC compiler cl.exe was not found and could not be
echo         set up automatically.
echo   * Install Visual Studio 2019 or 2022 with the
echo     "Desktop development with C++" workload, OR
echo   * run this script from a "x64 Native Tools Command Prompt for VS".
echo   vswhere / vcvars64.bat lookup failed under the standard install paths.
exit /b 1

:no_cmake
echo.
echo [build] ERROR: cmake.exe was not found.
echo   * Not on PATH, and not under your CMAKE_HOME\bin or Qt Tools\CMake_64\bin.
echo   * Install CMake (https://cmake.org/download) and add it to PATH, or set
echo     CMAKE_HOME, or install the Qt "CMake" tool via the Qt Maintenance Tool.
exit /b 1

:no_ninja
echo.
echo [build] ERROR: ninja.exe was not found.
echo   * Not on PATH, and not under your Qt Tools\Ninja folder.
echo   * Install Ninja: the Qt Maintenance Tool ("Ninja" under Developer and
echo     Designer Tools), or "winget install Ninja-build.Ninja", or
echo     "choco install ninja".
exit /b 1

:no_git
echo.
echo [build] ERROR: git.exe was not found, but the CMake configure needs it to
echo         FetchContent cmark-gfm, qtkeychain, tinyxml2 and MicroTeX.
echo   * Install Git for Windows (https://git-scm.com/download/win), make sure
echo     git is on PATH, then re-run.
exit /b 1

:no_qt
echo.
echo [build] ERROR: could not find a Qt6 MSVC kit.
echo   * Looked for the newest 6.x msvc*_64 kit under QT_HOME and C:\Qt.
echo   * Install Qt 6 (msvc2022_64 or msvc2019_64) INCLUDING the "Qt PDF"
echo     library via the Qt Maintenance Tool, OR set QT_PREFIX to the kit, e.g.:
echo       set QT_PREFIX=C:\Qt\6.11.0\msvc2022_64
exit /b 1

:bad_cfg
echo.
echo [build] ERROR: unknown build type "%~1". Use release (default) or debug.
exit /b 1

:cfg_fail
echo.
echo [build] CMake configure FAILED. Common causes:
echo   * The Qt kit lacks the "Qt PDF" component (Qt6Pdf / Qt6PdfQuick).
echo   * No network / git access for the FetchContent dependency clones.
exit /b 1

:build_fail
echo.
echo [build] Build FAILED.
exit /b 1
