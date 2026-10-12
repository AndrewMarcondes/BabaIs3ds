#!/bin/sh
# usage: test/level_test.sh <path to game Data dir>
set -e
cd "$(dirname "$0")/.."
mkdir -p build-host
cc -O1 -Wall -Wextra -fsanitize=address,undefined -o build-host/level_test test/level_test.c source/level.c -lz
find "$1/Worlds" -name '*.l' | sort > build-host/levels.txt
tr '\n' '\0' < build-host/levels.txt | xargs -0 build-host/level_test > build-host/c.txt
python3 -I -c '
import sys; sys.path.insert(0, "tools"); import baba_level
for p in open("build-host/levels.txt").read().splitlines():
    v, L = baba_level.parse(p)
    s = "".join(" %dx%d:%d:%d" % (l["w"], l["h"], sum(t*(i+1) for i,t in enumerate(l["tiles"])), sum(t*(i+1) for i,t in enumerate(l["data"].get(0, b"")))) for l in L)
    print(f"{p} v{v}{s}")' > build-host/py.txt
diff build-host/py.txt build-host/c.txt && echo "PASS: $(wc -l < build-host/c.txt) levels match"
