# baba3ds

Unofficial 3DS engine for Baba Is You. Ships no game files: you need your own copy of the game.

## Build
Needs devkitPro with `3ds-dev 3ds-zlib 3ds-libvorbisidec`.

    make

## Run
1. Copy your game's `Data` folder (Mac: `Baba Is You.app/Contents/Resources/Data`) to the SD card as `sdmc:/3ds/baba/Data`.
2. Sound effects are packed inside the Mac app, so extract them into that Data copy once (Python 3, no extra packages):

       python3 tools/extract_sounds.py "<...>/Baba Is You.app" "<SD card>/3ds/baba/Data/Sounds"

3. Copy `baba3ds.3dsx` to `sdmc:/3ds/` and launch from the Homebrew Launcher.
4. For music and sound, the console's DSP firmware must be at `sdmc:/3ds/dspfirm.cdc` (Luma3DS: Rosalina menu → Miscellaneous
   options → Dump DSP firmware). Azahar needs the same file in its `sdmc/3ds/` folder.

## Controls
D-pad/Circle Pad move, B undo, A wait (in a level) / enter level (on the map), X restart, SELECT back to map,
START pause menu (resume, restart, return to map, quit).
In Azahar (default keys): arrows move, S undo, A wait/enter, Z restart, N back to map, M menu.
Progress is saved to `sdmc:/3ds/baba/save.ini`.

## Host tests (Mac/Linux, need zlib)

    test/build_host.sh
    test/level_test.sh <path to Data>        # C level parser vs Python on every level
    build-host/script_test <path to Data>    # runs test/play_test.lua: level 1, win, map, enter level 2, leave
    build-host/script_test <path to Data> test/sweep_test.lua     # loads and plays a few turns of every level
    build-host/script_test <path to Data> test/progress_test.lua  # gates and map clears
    build-host/render_test <path to Data> out.ppm [save.ini] [keys...] ["=100level"]  # boot with a save, play keys or jump to a level, render

Emulator screenshot (Azahar running with RPC server enabled; copy AzaharRPC.py from Azahar's scripting/ folder into tools/):

    python3 tools/emu_shot.py build-host/emu

Lua 5.3.4 is vendored in `lua/` (MIT, see lua/LICENSE).

Reverse-engineering notes: NOTES.md.
