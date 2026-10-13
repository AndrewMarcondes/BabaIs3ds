# baba3ds

Unofficial 3DS engine for Baba Is You. Ships no game files: you need your own copy of the game.

## Build
Needs devkitPro with `3ds-dev 3ds-zlib 3ds-libvorbisidec`.

    make

## Run
1. Prepare the game data from your own copy (Python 3, no extra packages). With the SD card mounted:

       python3 tools/prepare_sd.py "<...>/Baba Is You.app" "/Volumes/<SD card>/3ds/baba"

   On a Steam install the app is at `~/Library/Application Support/Steam/steamapps/common/Baba Is You/Baba Is You.app`.
   This copies the game's data, packs the ~4,400 sprite files into one `sprites.pak` (much faster and smaller on an
   SD card) and extracts the sound effects. Nothing from the game is in this repository; the tool only rearranges
   files you already own. (Copying `Data` by hand to `sdmc:/3ds/baba/Data` also works, just slower and without
   sound effects.)
2. Copy `baba3ds.3dsx` to `sdmc:/3ds/` and launch from the Homebrew Launcher.
3. For music and sound, the console's DSP firmware must be at `sdmc:/3ds/dspfirm.cdc` (Luma3DS: Rosalina menu →
   Miscellaneous options → Dump DSP firmware). Azahar needs the same file in its `sdmc/3ds/` folder.

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
