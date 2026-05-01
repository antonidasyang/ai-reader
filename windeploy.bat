@echo off
REM ── windeployqt helper ──────────────────────────────────────────────
REM Stages the Qt DLLs, plugins and QML modules ai-reader.exe needs
REM next to the binary. Assumes a CMake Release build has just run; the
REM POST_BUILD step in CMakeLists.txt copies ai-reader.exe into dist\
REM so this script runs windeployqt directly against that staged copy.
REM
REM Requirements:
REM   * windeployqt.exe must be on PATH (run from a Qt 6 dev shell, or
REM     prepend it manually, e.g. set PATH=C:\Qt\6.8.0\msvc2022_64\bin;%PATH%)
REM   * dist\ai-reader.exe must exist (build the project first)
REM
REM Output: dist\ becomes a fully self-contained portable folder that
REM Inno Setup (AiReader.iss) packages into AiReader-Setup-x.y.z.exe.

setlocal
set "ROOT=%~dp0"
set "DIST=%ROOT%dist"

if not exist "%DIST%\ai-reader.exe" (
    echo [windeploy] dist\ai-reader.exe not found. Build the project first.
    echo            Expected: %DIST%\ai-reader.exe
    exit /b 1
)

where windeployqt >nul 2>nul
if errorlevel 1 (
    echo [windeploy] windeployqt.exe not on PATH. Open a Qt 6 dev shell, or add
    echo            the Qt bin directory to PATH, then re-run this script.
    exit /b 1
)

echo [windeploy] Staging Qt runtime into %DIST%
REM We deliberately keep d3d-compiler + the software OpenGL fallback so
REM the binary still launches on machines whose GPU drivers reject
REM ANGLE/D3D11 — those bytes are cheap insurance against "double-click,
REM nothing happens". --no-translations stays on because we ship our
REM own .qm files compiled into the binary.
windeployqt ^
    --release ^
    --qmldir "%ROOT%qml" ^
    --no-translations ^
    "%DIST%\ai-reader.exe"

if errorlevel 1 (
    echo [windeploy] windeployqt reported an error.
    exit /b 1
)

echo [windeploy] Done. dist\ is ready for Inno Setup.
endlocal
