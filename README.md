# baba3ds

Unofficial 3DS engine for Baba Is You. Ships no game files: you need your own copy of the game.

## Build
Needs devkitPro with `3ds-dev 3ds-zlib`.

    make

## Run
1. Copy your game's `Data` folder (Mac: `Baba Is You.app/Contents/Resources/Data`) to the SD card as `sdmc:/3ds/baba/Data`.
2. Copy `baba3ds.3dsx` to `sdmc:/3ds/` and launch from the Homebrew Launcher.

## Host tests (Mac/Linux, need zlib)

    test/level_test.sh <path to Data>
    cc -o build-host/render_test test/render_test.c source/render.c source/level.c source/png.c -lz
    build-host/render_test <path to Data> Worlds/baba/0level out.ppm

Reverse-engineering notes: NOTES.md.
