@echo off
REM ============================================================
REM  BACKROOMS - Windows build script (run from MinGW/MSYS2)
REM  Requires g++ (MinGW-w64) in PATH.
REM ============================================================
setlocal
set GLAD=deps\glad_out
set GLM=deps\glm_inc
set GLFW=deps\glfw-3.4.bin.WIN64
set MA=deps\miniaudio

if not exist build mkdir build
if not exist .objcache mkdir .objcache

if not exist .objcache\ma_impl_win.o (
    echo ==^> Compiling miniaudio ^(cached^) ...
    gcc -std=c11 -O1 -DNDEBUG -I %MA% -c src\ma_impl.c -o .objcache\ma_impl_win.o
)

echo ==^> Building Backrooms.exe ...
g++ -std=c++17 -O2 -DNDEBUG ^
    src\main.cpp %GLAD%\src\gl.c .objcache\ma_impl_win.o ^
    -I %GLAD%\include -I %GLM% -I %GLFW%\include -I %MA% -I src ^
    -L %GLFW%\lib-mingw-w64 ^
    -lglfw3 -lopengl32 -lgdi32 -lwinmm -lole32 ^
    -static -static-libgcc -static-libstdc++ ^
    -mwindows ^
    -o build\Backrooms.exe

if %ERRORLEVEL%==0 (
    echo    -^> build\Backrooms.exe created successfully.
) else (
    echo    Build FAILED.
)
endlocal
