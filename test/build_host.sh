#!/bin/sh
# Builds the host (Mac/Linux) test tools into build-host/. Needs a C compiler and zlib.
set -e
cd "$(dirname "$0")/.."
mkdir -p build-host/lua
[ -f build-host/lua/lapi.o ] || (cd build-host/lua && cc -O1 -w -DLUA_USE_POSIX -DLUA_COMPAT_5_2 -c ../../lua/*.c)
SRC="source/script.c source/level.c source/render.c source/png.c"
FLAGS="-O1 -Wall -Wextra -fsanitize=address,undefined"
for t in script_test render_test; do
    cc $FLAGS -o build-host/$t test/$t.c $SRC build-host/lua/*.o -lm -lz
done
