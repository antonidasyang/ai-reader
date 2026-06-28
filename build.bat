@echo off
REM ===========================================================================
REM  build.bat -- one-shot Windows build of ai-reader (Qt6), no manual env steps.
REM
REM  Usage:
REM      build.bat                 Release (default)
REM      build.bat debug           Debug
REM      build.bat clean           wipe build\ first, then Release
REM      build.bat release clean   wipe build\ first, then Release
REM      build.bat debug clean     wipe build\ first, then Debug
REM
REM  Generator: the Visual Studio (MSBuild) generator. ai-reader uses Ninja on
REM  macOS/Linux, but on Windows the Qt6 qmlcachegen step emits a build edge
REM  with multiple outputs plus a depfile, which OLD Ninja rejects with
REM  "multiple outputs aren't (yet?) supported by depslog". MSBuild has no such
REM  limit, is always present alongside MSVC, and builds in parallel -- so we
REM  use it here and avoid guessing the user's Ninja version. (A recent ninja
REM  1.12+ also handles it; this script just does not depend on it.)
REM
REM  Toolchain it finds for you (clear error + where-to-get-it if missing):
REM    * cmake -- PATH, then %CMAKE_HOME%\bin, then %QT_HOME%\Tools\CMake_64\bin,
REM      then the QT_PREFIX-derived %QT_TOOLS%\CMake_64\bin.
REM    * git   -- required: the configure FetchContent's cmark-gfm, qtkeychain,
REM      tinyxml2 and MicroTeX, which need git to clone.
REM    * Visual Studio with the C++ toolset -- located by CMake's VS generator;
REM      the edition is taken from the Qt kit (msvc2022 -> VS2022, msvc2019 ->
REM      VS2019). No vcvars call needed; CMake drives MSBuild directly.
REM
REM  Qt kit (QT_PREFIX): explicit QT_PREFIX wins; else newest 6.x msvc*_64 under
REM  %QT_HOME%; else under C:\Qt. Must include the "Qt PDF" component
REM  (Qt6Pdf/Qt6PdfQuick); cmake configure fails clearly if it is absent.
REM
REM  Incremental by default (a clean wipe re-downloads + rebuilds every
REM  FetchContent dep). Pass "clean" to force from-scratch. NOTE: switching the
REM  generator (e.g. from a previous Ninja build dir) also forces one clean.
REM
REM  ASCII-only: cmd.exe parses .bat in the system codepage (GBK on Chinese
REM  Windows), not UTF-8. No parenthesised if/else blocks and no '>' in echo.
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

REM --- Pick the VS generator edition from the kit's msvc tag -----------------
REM "%VAR:needle=%"=="%VAR%" is false only when VAR contains needle.
set "VS_GEN=Visual Studio 17 2022"
if not "%QT_PREFIX:msvc2019=%"=="%QT_PREFIX%" set "VS_GEN=Visual Studio 16 2019"
if not "%QT_PREFIX:msvc2022=%"=="%QT_PREFIX%" set "VS_GEN=Visual Studio 17 2022"

REM --- Derive the Qt Tools dir from QT_PREFIX --------------------------------
REM QT_PREFIX = ...\Qt\6.x.y\msvc2022_64  ->  Qt root = ...\Qt  ->  ...\Qt\Tools
set "QT_TOOLS="
for %%I in ("%QT_PREFIX%\..\..") do set "QT_ROOT=%%~fI"
if defined QT_ROOT set "QT_TOOLS=%QT_ROOT%\Tools"

echo.
echo ===========================================================================
echo  Building ai-reader (Qt6)
echo  Build type:  %BUILD_TYPE%
echo  Generator:   %VS_GEN%
echo  Qt kit:      %QT_PREFIX%
echo ===========================================================================
echo.

REM === cmake ================================================================
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

REM === git (the CMake configure FetchContent's the deps) ===================
where git >nul 2>nul && goto :have_git
goto :no_git

:have_git
echo [build] git ready.

REM --- Force a clean if build\ was configured with a different generator -----
REM CMake refuses to reconfigure a build dir under a new generator; a leftover
REM Ninja build\ would error. Auto-clean instead of failing.
if not exist "build\CMakeCache.txt" goto :gen_checked
findstr /C:"CMAKE_GENERATOR:INTERNAL=%VS_GEN%" "build\CMakeCache.txt" >nul 2>nul && goto :gen_checked
echo [build] build\ was configured with a different generator; forcing clean...
set "DOCLEAN=1"
:gen_checked

REM --- Optional / forced clean ---------------------------------------------
if defined DOCLEAN if exist build echo [build] clean: removing stale build directory...
if defined DOCLEAN if exist build rmdir /S /Q build

REM --- Configure ------------------------------------------------------------
REM CMAKE_POLICY_VERSION_MINIMUM=3.5 is needed when cmake is 4.x (it rejects the
REM pre-3.5 cmake_minimum_required in FetchContent deps like cmark-gfm). It is
REM harmless on cmake 3.x (you may see a one-line "not used" note).
echo [build] Configuring (%BUILD_TYPE%) ...
cmake -B build -G "%VS_GEN%" -A x64 -DCMAKE_PREFIX_PATH="%QT_PREFIX%" -DCMAKE_POLICY_VERSION_MINIMUM=3.5
if errorlevel 1 goto :cfg_fail

echo [build] Compiling (%BUILD_TYPE%, parallel) ...
cmake --build build --config %BUILD_TYPE% --parallel
if errorlevel 1 goto :build_fail

echo.
echo [build] SUCCESS. Output: build\%BUILD_TYPE%\ai-reader.exe
echo [build] CMake also staged dist\ai-reader.exe + dist\microtex_res.
echo [build] Next, to make a runnable / installable build:
echo     package.bat         windeploy + Inno Setup in one shot -^> installer\
echo   or run the steps yourself:
echo     windeploy.bat       stage the Qt runtime + MSVC DLLs into dist\
echo     iscc AiReader.iss   build the Windows installer (needs Inno Setup)
echo.
endlocal
exit /b 0

:no_cmake
echo.
echo [build] ERROR: cmake.exe was not found.
echo   * Not on PATH, and not under your CMAKE_HOME\bin or Qt Tools\CMake_64\bin.
echo   * Install CMake (https://cmake.org/download) and add it to PATH, or set
echo     CMAKE_HOME, or install the Qt "CMake" tool via the Qt Maintenance Tool.
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
echo   * Visual Studio with the "Desktop development with C++" workload is not
echo     installed (CMake's VS generator could not find a toolset).
echo   * The Qt kit lacks the "Qt PDF" component (Qt6Pdf / Qt6PdfQuick).
echo   * No network / git access for the FetchContent dependency clones.
exit /b 1

:build_fail
echo.
echo [build] Build FAILED.
exit /b 1
