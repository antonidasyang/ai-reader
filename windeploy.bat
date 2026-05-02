@echo off
REM windeployqt helper.
REM
REM Stages the Qt DLLs, plugins and QML modules ai-reader.exe needs
REM next to the binary. Assumes a CMake Release build has just run; the
REM POST_BUILD step in CMakeLists.txt copies ai-reader.exe into dist\
REM so this script runs windeployqt directly against that staged copy.
REM
REM How windeployqt is located, in priority order:
REM   1. The WINDEPLOYQT environment variable (explicit override).
REM   2. windeployqt.exe on PATH.
REM   3. Derived from build\CMakeCache.txt:Qt6_DIR -- strips the
REM      \lib\cmake\Qt6 suffix and appends \bin\windeployqt.exe. This
REM      means a working CMake configure is sufficient; you don't need
REM      to add the Qt bin folder to PATH separately.
REM   4. Hard-coded common install paths (C:\Qt\6.x.x\msvc2022_64\bin).
REM
REM IMPORTANT: this file is ASCII-only on purpose. cmd.exe parses .bat
REM as the system codepage (GBK on Chinese Windows, cp1252 elsewhere),
REM not as UTF-8, so any non-ASCII byte in a comment can break the
REM parser. Do not add box-drawing or smart-punctuation characters.

setlocal EnableDelayedExpansion
set "ROOT=%~dp0"
set "DIST=%ROOT%dist"

if not exist "%DIST%\ai-reader.exe" (
    echo [windeploy] dist\ai-reader.exe not found. Build the project first.
    echo            Expected: %DIST%\ai-reader.exe
    exit /b 1
)

REM 1. Explicit override.
if defined WINDEPLOYQT (
    if exist "%WINDEPLOYQT%" goto :found
    echo [windeploy] WINDEPLOYQT='%WINDEPLOYQT%' is set but the file does not exist.
    exit /b 1
)

REM 2. PATH lookup.
for /f "delims=" %%p in ('where windeployqt 2^>nul') do (
    set "WINDEPLOYQT=%%p"
    goto :found
)

REM 3. Derive from CMakeCache.txt. Check, in order:
REM      a) %ROOT%\build\CMakeCache.txt                  (our default)
REM      b) %ROOT%\build\<subdir>\CMakeCache.txt         (Qt Creator
REM         nested layout, e.g. Desktop_Qt_6_11_0_MSVC2022_64bit-Release)
REM      c) %ROOT%\..\build-*\<subdir>?\CMakeCache.txt   (Qt Creator
REM         sibling-folder layout, less common)
REM    First hit wins.
set "CACHE="
if exist "%ROOT%build\CMakeCache.txt" (
    set "CACHE=%ROOT%build\CMakeCache.txt"
    goto :have_cache
)
if exist "%ROOT%build" (
    for /d %%d in ("%ROOT%build\*") do (
        if exist "%%d\CMakeCache.txt" (
            set "CACHE=%%d\CMakeCache.txt"
            goto :have_cache
        )
    )
)
for /d %%d in ("%ROOT%..\build-*") do (
    if exist "%%d\CMakeCache.txt" (
        set "CACHE=%%d\CMakeCache.txt"
        goto :have_cache
    )
    for /d %%e in ("%%d\*") do (
        if exist "%%e\CMakeCache.txt" (
            set "CACHE=%%e\CMakeCache.txt"
            goto :have_cache
        )
    )
)
goto :no_cache

:have_cache
echo [windeploy] Reading "!CACHE!"
for /f "tokens=2 delims==" %%i in ('findstr /b /c:"Qt6_DIR:" "!CACHE!" 2^>nul') do (
    set "QT_CMAKE_DIR=%%i"
)
if defined QT_CMAKE_DIR (
    REM Qt6_DIR looks like   C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6
    REM We want              C:\Qt\6.11.0\msvc2022_64\bin\windeployqt.exe
    set "QT_BASE=!QT_CMAKE_DIR:/lib/cmake/Qt6=!"
    set "QT_BASE=!QT_BASE:/=\!"
    set "WINDEPLOYQT=!QT_BASE!\bin\windeployqt.exe"
    if exist "!WINDEPLOYQT!" goto :found
    echo [windeploy] Derived path from CMakeCache.txt does not exist:
    echo            !WINDEPLOYQT!
    set "WINDEPLOYQT="
)
:no_cache

REM 4. Hard-coded common install paths.
for %%v in (6.11.0 6.10.0 6.9.0 6.8.0 6.7.0 6.6.0 6.5.0 6.4.0) do (
    if exist "C:\Qt\%%v\msvc2022_64\bin\windeployqt.exe" (
        set "WINDEPLOYQT=C:\Qt\%%v\msvc2022_64\bin\windeployqt.exe"
        goto :found
    )
    if exist "C:\Qt\%%v\msvc2019_64\bin\windeployqt.exe" (
        set "WINDEPLOYQT=C:\Qt\%%v\msvc2019_64\bin\windeployqt.exe"
        goto :found
    )
)

echo [windeploy] Could not locate windeployqt.exe.
echo.
echo Try one of:
echo   * Add Qt's bin dir to PATH, e.g.:
echo       set PATH=C:\Qt\6.11.0\msvc2022_64\bin;%%PATH%%
echo   * Set WINDEPLOYQT explicitly:
echo       set WINDEPLOYQT=C:\Qt\6.11.0\msvc2022_64\bin\windeployqt.exe
echo   * Re-run cmake -B build ...   to refresh CMakeCache.txt's Qt6_DIR.
exit /b 1

:found
echo [windeploy] Using "!WINDEPLOYQT!"
echo [windeploy] Staging Qt runtime into %DIST%
REM Flags worth knowing about:
REM   --compiler-runtime   Pull vcruntime140.dll / vcruntime140_1.dll /
REM                        msvcp140.dll out of MSVC's redist tree and copy
REM                        them next to the .exe. Without this, users on a
REM                        stock Windows install hit "VCRUNTIME140_1.dll
REM                        not found" because they have never installed
REM                        the Visual C++ Redistributable. Adds about 1MB.
REM   --no-translations    We ship our own .qm files compiled into the
REM                        binary; skipping Qt's catalog saves ~30MB.
REM   d3d-compiler + software OpenGL fallback are kept (default) so the
REM   binary still launches on machines whose GPU drivers reject
REM   ANGLE/D3D11.
"!WINDEPLOYQT!" ^
    --release ^
    --qmldir "%ROOT%qml" ^
    --no-translations ^
    --compiler-runtime ^
    "%DIST%\ai-reader.exe"

if errorlevel 1 (
    echo [windeploy] windeployqt reported an error.
    exit /b 1
)

echo [windeploy] Done. dist\ is ready for Inno Setup.
endlocal
