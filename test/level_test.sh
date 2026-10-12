#!/bin/sh
# usage: test/level_test.sh <path to game Data dir>
set -e
cd "$(dirname "$0")/.."
cc -O1 -Wall -Wextra -fsanitize=address,undefined -o build/level_test test/level_test.c source/level.c -lz
find "$1/Worlds" -name '*.l' | sort > build/levels.txt
tr '\n' '\0' < build/levels.txt | xargs -0 build/level_test > build/c.txt
python3 -I -c '
import sys; sys.path.insert(0, "tools"); import baba_level
for p in open("build/levels.txt").read().splitlines():
    v, L = baba_level.parse(p)
    s = "".join(" %dx%d:%d:%d" % (l["w"], l["h"], sum(t*(i+1) for i,t in enumerate(l["tiles"])), sum(t*(i+1) for i,t in enumerate(l["data"].get(0, b"")))) for l in L)
    print(f"{p} v{v}{s}")' > build/py.txt
diff build/py.txt build/c.txt && echo "PASS: $(wc -l < build/c.txt) levels match"
