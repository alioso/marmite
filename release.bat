@echo off
REM Release build for distribution, into dist\ — run from the
REM "x64 Native Tools Command Prompt for VS", from the repo root.
REM Separate build tree from any dev build (build-dist vs build-dev) so
REM the two never collide. Mirrors ./release (macOS); keep them in sync.
setlocal

git pull
if exist build-dist rmdir /s /q build-dist
if exist dist rmdir /s /q dist
cmake -S . -B build-dist -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-dist
cmake --install build-dist --prefix dist --component release

echo.
echo Done. Release build installed to:
echo   dist\Standalone\Marmite.exe
echo   dist\VST3\Marmite.vst3
