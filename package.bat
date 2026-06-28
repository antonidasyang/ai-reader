@echo off
REM ===========================================================================
REM  package.bat -- build a Windows installer for AI Reader in one shot.
REM
REM  Usage:
REM      package.bat            windeploy + Inno Setup (assumes a Release build
REM                             already exists in build\ / dist\)
REM      package.bat build      run build.bat (Release) first, then package
REM      package.bat clean      build.bat clean (from scratch), then package
REM
REM  Produces:
REM      installer\AiReader-Setup-<version>.exe
REM  where <version> is read straight out of the built ai-reader.exe (which CMake
REM  stamps from project(... VERSION ...) in CMakeLists.txt). Nothing to bump in
REM  AiReader.iss or here -- edit CMakeLists.txt and rebuild.
REM
REM  Steps it runs:
REM      1. (optional) build.bat        compile Release -> dist\ai-reader.exe
REM      2. windeploy.bat               windeployqt + MSVC runtime DLLs into dist\
REM      3. ISCC AiReader.iss           pack dist\ into the single-file installer
REM
REM  Toolchain: build.bat's (cmake + Qt6 + Visual Studio) for the optional build,
REM  plus Inno Setup 6 (ISCC.exe). It is found on PATH, else under
REM  %ProgramFiles(x86)%\Inno Setup 6 or %ProgramFiles%\Inno Setup 6.
REM
REM  ASCII-only: cmd.exe parses .bat in the system codepage (GBK on Chinese
REM  Windows), not UTF-8. No parenthesised if/else with non-ASCII, no '>' in echo.
REM ===========================================================================

setlocal EnableDelayedExpansion
set "ROOT=%~dp0"

REM --- Optional build -------------------------------------------------------
set "DOBUILD="
if /I "%~1"=="build" set "DOBUILD=release"
if /I "%~1"=="clean" set "DOBUILD=clean"
if defined DOBUILD (
    echo [package] Building Release ^(%DOBUILD%^) ...
    if "%DOBUILD%"=="clean" (
        call "%ROOT%build.bat" release clean
    ) else (
        call "%ROOT%build.bat" release
    )
    if errorlevel 1 goto :build_failed
)

if not exist "%ROOT%dist\ai-reader.exe" goto :no_exe

REM --- Stage Qt runtime + VC runtime into dist\ ----------------------------
echo [package] Staging runtime ^(windeploy.bat^) ...
call "%ROOT%windeploy.bat"
if errorlevel 1 goto :windeploy_failed

REM --- Locate Inno Setup's ISCC.exe ----------------------------------------
set "ISCC="
for /f "delims=" %%p in ('where iscc 2^>nul') do if not defined ISCC set "ISCC=%%p"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC goto :no_iscc
echo [package] Using "!ISCC!"

REM --- Build the installer (version read from dist\ai-reader.exe) -----------
"!ISCC!" "%ROOT%AiReader.iss"
if errorlevel 1 goto :iscc_failed

echo.
echo [package] SUCCESS. Installer written to installer\AiReader-Setup-^<version^>.exe
echo           ^(version stamped from the exe = CMakeLists project VERSION^).
endlocal
exit /b 0

:no_exe
echo.
echo [package] ERROR: dist\ai-reader.exe not found.
echo            Run a Release build first, or:  package.bat build
exit /b 1

:no_iscc
echo.
echo [package] ERROR: Inno Setup's ISCC.exe was not found.
echo            Install Inno Setup 6 ^(https://jrsoftware.org/isdl.php^), or add
echo            ISCC.exe to PATH, then re-run.
exit /b 1

:build_failed
echo [package] build.bat failed.
exit /b 1

:windeploy_failed
echo [package] windeploy.bat failed.
exit /b 1

:iscc_failed
echo [package] Inno Setup ^(ISCC^) failed.
exit /b 1
