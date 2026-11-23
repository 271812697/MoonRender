@echo off
rem MOON CAD - single-threaded render mode (commands are recorded and executed on the same thread).
rem Use this to compare input feel against the threaded mode (run-thread.bat).
rem Any argument is passed to the app: a .moon file, a .step / .obj model, or nothing.
setlocal
set "MOON_RENDER_THREAD=0"
set "QT_OPENGL=desktop"
set "MOON_EXE=%~dp0Build\bin\Release\Moon.exe"
if not exist "%MOON_EXE%" (
    echo [run-single] "%MOON_EXE%" not found - build it first:
    echo     cmake --build Build --target Moon --config Release -- /nologo /m:1
    pause
    exit /b 1
)
echo [run-single] MOON_RENDER_THREAD=%MOON_RENDER_THREAD%  (single thread)
start "MOON (single thread)" "%MOON_EXE%" %*
endlocal