#!/bin/sh
# Builds baba3ds.cia (installable to the HOME Menu with FBI) next to baba3ds.3dsx.
# Needs makerom and bannertool: on PATH, or set MAKEROM / BANNERTOOL. See README.
set -e
cd "$(dirname "$0")/.."
MAKEROM=${MAKEROM:-makerom}
BANNERTOOL=${BANNERTOOL:-bannertool}
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro} DEVKITARM=${DEVKITARM:-/opt/devkitpro/devkitARM}

[ -f meta/icon.png ] && [ -f meta/banner.png ] && [ -f meta/banner.wav ] || python3 tools/make_art.py
make
# title version from the Makefile's major.minor, as the 3DS packs it (major << 10 | minor << 4); FBI shows it
VERSION=$(sed -n 's/^VERSION[[:space:]]*:=[[:space:]]*\([0-9]*\.[0-9]*\).*/\1/p' Makefile)
"$BANNERTOOL" makebanner -i meta/banner.png -a meta/banner.wav -o build/banner.bnr >/dev/null
"$MAKEROM" -f cia -o baba3ds.cia -target t -exefslogo -rsf meta/cia.rsf \
    -elf baba3ds.elf -icon baba3ds.smdh -banner build/banner.bnr -ver $(( ${VERSION%.*} * 1024 + ${VERSION#*.} * 16 ))
echo "built baba3ds.cia v$VERSION ($(du -h baba3ds.cia | cut -f1))"
