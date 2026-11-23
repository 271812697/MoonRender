@echo off
rem MOON CAD - threaded render mode (record on this thread, execute on the driver thread).
rem This is the default; the script exists so the two modes can be started side by side.
setlocal
if defined MOON_RENDER_THREAD set "MOON_RENDER_THREAD="
set "QT_OPENGL=desktop"
set "MOON_EXE=%~dp0Build\bin\Release\Moon.exe"
if not exist "%MOON_EXE%" (
    echo [run-thread] "%MOON_EXE%" not found - build it first:
    echo     cmake --build Build --target Moon --config Release -- /nologo /m:1
    pause
    exit /b 1
)
echo [run-thread] render thread active  (default mode)
start "MOON (threaded)" "%MOON_EXE%" %*
endlocal