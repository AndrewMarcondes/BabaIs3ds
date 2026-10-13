# baba3ds

Unofficial 3DS engine for Baba Is You. Ships no game files: you need your own copy of the game.

## Build
Needs devkitPro with `3ds-dev 3ds-zlib 3ds-libvorbisidec`.

    make

## Installable .cia (HOME Menu icon)
Needs [makerom](https://github.com/3DSGuy/Project_CTR/releases) and
[bannertool](https://github.com/diasurgical/bannertool) (no macOS release: compile `source/*.cpp source/pc/*.c
source/3ds/*.cpp` with clang++). Then:

    MAKEROM=/path/to/makerom BANNERTOOL=/path/to/bannertool tools/make_cia.sh

Copy `baba3ds.cia` to the SD card and install it with FBI. The icon, banner and chime are original
(`tools/make_art.py` draws them into `meta/`); the package settings are in `meta/cia.rsf`.
The game data still goes on the SD card as below, for the .cia and the .3dsx alike.

## Run
1. Prepare the game data from your own copy (Python 3, no extra packages). With the SD card mounted:

       python3 tools/prepare_sd.py "<...>/Baba Is You.app" "/Volumes/<SD card>/3ds/baba"

   On a Steam install the app is at `~/Library/Application Support/Steam/steamapps/common/Baba Is You/Baba Is You.app`.
   Add `--small-audio` (needs `brew install vorbis-tools`) to re-encode music and sound effects smaller:
   ~100 MB -> ~60 MB in total, and less work for the 3DS to decode.
   This copies the game's data, packs the ~4,400 sprite files into one `sprites.pak` (much faster and smaller on an
   SD card) and extracts the sound effects. Nothing from the game is in this repository; the tool only rearranges
   files you already own. (Copying `Data` by hand to `sdmc:/3ds/baba/Data` also works, just slower and without
   sound effects.)
2. Copy `baba3ds.3dsx` to `sdmc:/3ds/` and launch from the Homebrew Launcher.
3. For music and sound, the console's DSP firmware must be at `sdmc:/3ds/dspfirm.cdc` (Luma3DS: Rosalina menu →
   Miscellaneous options → Dump DSP firmware). Azahar needs the same file in its `sdmc/3ds/` folder.

## Level packs and mods
The title screen has **Start game** (the main game), **Level packs & mods** and **Exit**.
- Every folder in `Data/Worlds` with a `world_data.txt` is a level pack. Your copy of the game already has
  *New Adventures* and the *Museum*.
- Mods use the PC game's own format: a pack's `Lua/*.lua` files are loaded when it's played, and
  `Data/Lua/*.lua` mods always. A pack can bring its own `Sprites/`, `Music/` and `Palettes/` too.
- To add a community pack or mod made for the PC version, copy its folder into `sdmc:/3ds/baba/Data/Worlds/`
  (and re-run `prepare_sd.py` if you keep packs on your Mac, so its sprites get packed).

## Controls
D-pad/Circle Pad move, B undo, A wait (in a level) / enter level (on the map), X restart, SELECT back to map,
START pause menu (resume, restart, return to map, title screen, quit).
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
