@echo off

if "%~1" == "--clear" (
    echo Clearing previous builds...

    if exist "build\csgo_gc\Release\csgo_gc.dll" del "build\csgo_gc\Release\csgo_gc.dll"
    if exist "build\launcher\Release\cc.exe" del "build\launcher\Release\cc.exe"
    if exist "build\launcher\Release\srcds.exe" del "build\launcher\Release\srcds.exe"
)

cmake --build build --config Release

if not exist "compiled" mkdir compiled

if exist "build\csgo_gc\Release\csgo_gc.dll" copy "build\csgo_gc\Release\csgo_gc.dll" "compiled\csgo_gc.dll"
if exist "build\launcher\Release\cc.exe" copy "build\launcher\Release\cc.exe" "compiled\cc.exe"
if exist "build\launcher\Release\srcds.exe" copy "build\launcher\Release\srcds.exe" "compiled\srcds.exe"

echo:
echo Finished compiling! Press any key to continue...

pause > nul