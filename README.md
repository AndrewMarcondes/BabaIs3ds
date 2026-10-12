# baba3ds

Unofficial 3DS engine for Baba Is You. Ships no game files: you need your own copy of the game.

## Build
Needs devkitPro with `3ds-dev 3ds-zlib`.

    make

## Run
1. Copy your game's `Data` folder (Mac: `Baba Is You.app/Contents/Resources/Data`) to the SD card as `sdmc:/3ds/baba/Data`.
2. Copy `baba3ds.3dsx` to `sdmc:/3ds/` and launch from the Homebrew Launcher.

## Controls
D-pad/Circle Pad move, B undo, A wait, X restart, START quit.
In Azahar (default keys): arrows move, S undo, A wait, Z restart, M quit.

## Host tests (Mac/Linux, need zlib)

    test/build_host.sh
    test/level_test.sh <path to Data>        # C level parser vs Python on every level
    build-host/script_test <path to Data>    # plays level 1 through the game's scripts: move, push, undo, restart
    build-host/render_test <path to Data> baba 0level out.ppm right right   # render after keys

Emulator screenshot (Azahar running with RPC server enabled; copy AzaharRPC.py from Azahar's scripting/ folder into tools/):

    python3 tools/emu_shot.py build-host/emu

Lua 5.3.4 is vendored in `lua/` (MIT, see lua/LICENSE).

Reverse-engineering notes: NOTES.md.
