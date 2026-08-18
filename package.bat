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
REM  plus Inno Setup 7 (ISCC.exe). It is found on PATH, else under
REM  %ProgramFiles%\Inno Setup 7 or %ProgramFiles(x86)%\Inno Setup 7.
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

REM --- Hard gate: never ship an installer without the MSVC runtime ---------
REM copy-vc-runtime.ps1 only WARNS when Visual Studio's Redist tree is
REM missing, so a machine without the C++ workload would otherwise
REM produce an installer that crashes on user PCs with
REM "VCRUNTIME140_1.dll not found". Verify the DLLs actually landed.
REM Set AIREADER_SKIP_VCCHECK=1 only if you consciously ship without
REM them (e.g. a portable build for machines with the redist).
if defined AIREADER_SKIP_VCCHECK goto :vc_ok
if not exist "%ROOT%dist\vcruntime140.dll"   goto :vc_missing
if not exist "%ROOT%dist\vcruntime140_1.dll" goto :vc_missing
if not exist "%ROOT%dist\msvcp140.dll"       goto :vc_missing
:vc_ok

REM --- Stage the official VC++ redistributable (belt and braces) -----------
REM The installer runs it silently when present (idempotent; newer
REM runtime on the system = no-op) which also covers UCRT corner
REM cases the app-local DLLs cannot. Cached in installer\cache\
REM (gitignored); .tmp + atomic rename so a truncated download can
REM never poison later builds. Failure is non-fatal: app-local CRT
REM DLLs above already cover stock Win10/11 machines.
if not exist "%ROOT%installer\cache" mkdir "%ROOT%installer\cache"
if exist "%ROOT%installer\cache\vc_redist.x64.exe" goto :redist_ok
echo [package] Downloading vc_redist.x64.exe ^(one-time, cached^) ...
curl -L -sS -o "%ROOT%installer\cache\vc_redist.x64.tmp" https://aka.ms/vs/17/release/vc_redist.x64.exe
if errorlevel 1 goto :redist_warn
move /y "%ROOT%installer\cache\vc_redist.x64.tmp" "%ROOT%installer\cache\vc_redist.x64.exe" >nul
goto :redist_ok
:redist_warn
echo [package] WARNING: vc_redist.x64.exe download failed; the installer
echo            will ship without it. App-local CRT DLLs are still included.
del "%ROOT%installer\cache\vc_redist.x64.tmp" 2>nul
:redist_ok

REM --- Locate Inno Setup's ISCC.exe ----------------------------------------
set "ISCC="
for /f "delims=" %%p in ('where iscc 2^>nul') do if not defined ISCC set "ISCC=%%p"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 7\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 7\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe"
if not defined ISCC goto :no_iscc
echo [package] Using "!ISCC!"

REM --- Version: read straight from CMakeLists.txt's project(... VERSION ...)
REM     and hand it to ISCC. This is the single source of truth and does NOT
REM     depend on the exe carrying a readable VERSIONINFO resource. The .iss
REM     still falls back to reading the exe if no /D is passed (manual iscc).
set "VER="
for /f "tokens=3" %%v in ('findstr /b /c:"project(ai-reader VERSION" "%ROOT%CMakeLists.txt"') do set "VER=%%v"
if defined VER (
    echo [package] Version from CMakeLists.txt: !VER!
    "!ISCC!" /DMyAppVersion=!VER! "%ROOT%AiReader.iss"
) else (
    echo [package] WARNING: could not read version from CMakeLists.txt;
    echo            falling back to the exe's embedded version.
    "!ISCC!" "%ROOT%AiReader.iss"
)
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
echo            Install Inno Setup 7 ^(https://jrsoftware.org/isdl.php^), or add
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

:vc_missing
echo.
echo [package] ERROR: MSVC runtime DLLs are missing from dist\.
echo            Expected vcruntime140.dll, vcruntime140_1.dll, msvcp140.dll.
echo            copy-vc-runtime.ps1 could not find Visual Studio's Redist
echo            tree - install the "C++ x64/x86 build tools" VS workload,
echo            or stage the DLLs into dist\ manually, then re-run.
echo            Shipping without them crashes on user PCs that lack the
echo            VC++ Redistributable. Set AIREADER_SKIP_VCCHECK=1 to
echo            override consciously.
exit /b 1
