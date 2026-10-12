# baba3ds

Unofficial 3DS engine for Baba Is You. Ships no game files: you need your own copy of the game.

## Build
Needs devkitPro with `3ds-dev 3ds-zlib`.

    make

## Run
1. Copy your game's `Data` folder (Mac: `Baba Is You.app/Contents/Resources/Data`) to the SD card as `sdmc:/3ds/baba/Data`.
2. Copy `baba3ds.3dsx` to `sdmc:/3ds/` and launch from the Homebrew Launcher.

## Controls
D-pad/Circle Pad move, B undo, A wait (in a level) / enter level (on the map), X restart, SELECT back to map, START quit.
In Azahar (default keys): arrows move, S undo, A wait/enter, Z restart, N back to map, M quit.
Progress is saved to `sdmc:/3ds/baba/save.ini`.

## Host tests (Mac/Linux, need zlib)

    test/build_host.sh
    test/level_test.sh <path to Data>        # C level parser vs Python on every level
    build-host/script_test <path to Data>    # runs test/play_test.lua: level 1, win, map, enter level 2, leave
    build-host/script_test <path to Data> test/sweep_test.lua   # loads and plays a few turns of every level
    build-host/render_test <path to Data> out.ppm [save.ini] [keys...] ["=100level"]  # boot with a save, play keys or jump to a level, render

Emulator screenshot (Azahar running with RPC server enabled; copy AzaharRPC.py from Azahar's scripting/ folder into tools/):

    python3 tools/emu_shot.py build-host/emu

Lua 5.3.4 is vendored in `lua/` (MIT, see lua/LICENSE).

Reverse-engineering notes: NOTES.md.
