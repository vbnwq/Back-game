#!/usr/bin/env bash
# =============================================================
#  BACKROOMS - build script
#  Builds a self-contained Windows .exe (cross-compile with MinGW)
#  and/or a native Linux binary.
# =============================================================
set -e
cd "$(dirname "$0")"

GLAD=deps/glad_out
GLM=deps/glm_inc
GLFW_WIN=deps/glfw-3.4.bin.WIN64
MA=deps/miniaudio

mkdir -p build .objcache

# ---- miniaudio: compiled once, at -O1, and cached (it is huge) ----
MA_OBJ_WIN=.objcache/ma_impl_win.o
if [ ! -f "$MA_OBJ_WIN" ] || [ src/ma_impl.c -nt "$MA_OBJ_WIN" ]; then
    echo "==> Compiling miniaudio (Windows, cached)..."
    x86_64-w64-mingw32-gcc -std=c11 -O0 -DNDEBUG \
        -I $MA -c src/ma_impl.c -o "$MA_OBJ_WIN"
fi

echo "==> Building Windows .exe (MinGW cross-compile)..."
x86_64-w64-mingw32-g++ -std=c++17 -O2 -DNDEBUG \
    src/main.cpp $GLAD/src/gl.c "$MA_OBJ_WIN" \
    -I $GLAD/include -I $GLM -I $GLFW_WIN/include -I $MA -I src \
    -L $GLFW_WIN/lib-mingw-w64 \
    -lglfw3 -lopengl32 -lgdi32 -lwinmm -lole32 \
    -static -static-libgcc -static-libstdc++ \
    -mwindows \
    -o build/Backrooms.exe
echo "    -> build/Backrooms.exe"

if command -v pkg-config >/dev/null && pkg-config --exists glfw3 2>/dev/null; then
    MA_OBJ_LIN=.objcache/ma_impl_lin.o
    if [ ! -f "$MA_OBJ_LIN" ] || [ src/ma_impl.c -nt "$MA_OBJ_LIN" ]; then
        echo "==> Compiling miniaudio (Linux, cached)..."
        gcc -std=c11 -O0 -I $MA -c src/ma_impl.c -o "$MA_OBJ_LIN"
    fi
    echo "==> Building native Linux binary..."
    g++ -std=c++17 -O2 src/main.cpp $GLAD/src/gl.c "$MA_OBJ_LIN" \
        -I $GLAD/include -I $GLM -I $MA -I src $(pkg-config --cflags glfw3) \
        -lglfw -lGL -ldl -lpthread -lm \
        -o build/Backrooms
    echo "    -> build/Backrooms"
fi

echo "==> Done."
