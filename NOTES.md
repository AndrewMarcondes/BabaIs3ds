# Baba Is You → 3DS: findings

Source: Steam Mac install, `Baba Is You.app/Contents/Resources/Data/`. Never commit or ship anything from there.

## Engine
- Mac binary is `Contents/MacOS/Chowdren` (Clickteam Fusion game translated to C++ by Chowdren).
- Embedded Lua is **5.3.4**. All game logic is plain `.lua` in `Data/` (~29k lines core, plus `Editor/`).
- Lua ↔ engine boundary: `MF_*` functions (`tools/mf_used.tsv` = called by scripts with counts, 157; `tools/mf_binary.txt` = names in binary, 199)
  plus `mmf.newObject(id)` (269 calls) returning objects with `.values`, `.strings`, `.flags` tables (e.g. `generaldata.values[...]`).
- Names used in scripts but not found as strings in binary: MF_alert, MF_bonus, MF_debug, MF_getprofileID, MF_intro, MF_keyid, MF_store. Find where they come from before stubbing.

## How the engine drives the scripts
- Engine calls Lua globals by name (175 function names defined in the scripts also appear as strings in the binary, list via `strings`).
  Key ones: `init(tilemapid, roomw, roomh, tilesize, xoff, yoff, gd1..gd5, spritedata, vardata, screenw, screenh)` (load.lua),
  `command(key, player)` for input (syntax.lua), `addunit(id)` per spawned object, `block()`, `levelblock()`, `firstlevel()`.
- Engine loads `Data/*.lua` and `Data/Editor/*.lua` (init needs `formatobjlist` from Editor/editor_objectlist.lua).
- Units are engine objects: `mmf.newObject(id)` -> `.values[]` `.strings[]` `.flags[]` indexed by constants.lua (XPOS, YPOS, UNITNAME, TILING...), plus `.fixed`, `.className`, `.direction`.
- Our side: `romfs/shim.lua` implements this in Lua (objects are tables); unknown `MF_*` become logging stubs (`engine.missing`).
- Level start (shim `engine.loadlevel`, modelled on `restore()` in debug.lua): `init(...)`, `clearunits()`, one object per .l tile
  with `className` = tileslist key (`objectNNN`) + XPOS/YPOS/DIR/ID=-1, `addunit(id)`; then `dynamictiling()`, `updatecolours()`, `startblock()`.
  `getmetadata` fills name/type/tiling/layer from tileslist, so the engine only sets className.
- Tile id 0 in .l = level edge; it is not a unit but `map[0]:get_x(x,y)` (255 = empty, else obstacle).
- Per turn (shim `engine.input`): `command(key)`, `block()`, `newundo()`. Undo: `undo()` then `code()`. `command('undo')` does nothing.
- Unit drawing: tiling 1 frame = `unit.direction` (set by dynamictiling), tiling 0/2/3 = DIR*8. Colour set by game via `MF_setcolour`.
- Particles: off via `generaldata.values[DISABLEPARTICLES] = 1`.
- Status: level 1 plays on host test (move, push, undo, restart asserted) and runs on Azahar.
- Win: game's block() calls MF_win(); shim saves completion 3 and goes up a level (uplevel) to the map.
- Data/MF_documentation.txt (ships with the game) documents most MF_* functions. Read it first.

## Map (verified on host: win level 1 -> map -> path revealed -> enter level 2 -> leave)
- Boot: init, setupmenu (map code needs `editor`), WORLD/BASEWORLD = "baba", `firstlevel()` picks level (fresh save: 0level; else start map).
- Level tree: sublevel(file) on enter, uplevel() on win/leave; CURRLEVEL is the level to load.
- Map .ld: `[general] leveltype=1`, `levels=N` (only first N `[levels]` entries are live; stale ones follow),
  `paths=N`, `specials=N`, `selectorX/Y`, `[images] total/0/1` = full-colour backgrounds in Worlds/<w>/Images/<name>_1.png (792x432).
- Engine creates one `level` unit per [levels] entry (U_LEVELFILE, U_LEVELNAME from target .ld name, COLOUR/CLEARCOLOUR,
  VISUALLEVEL=number, VISUALSTYLE=style 0 num/1 letter/2 dot/-1 icon, COMPLETED). `.ld state` is editor leftover: only 2 (open) is a default; save decides the rest.
- Paths: `addpath(i)` (game); engine must set path strings[LEVELFILE] = current level (save key "<level>,x,y").
- Map load order: tiles, level icons, addpath, addspecial, loadlevelcompletion, dynamictiling, updatecolours, startblock,
  openlevels (reveals via unlocklevels/hiddendata), loadlevelcompletion again (stand-in for unlock animation), mapcursor_load, mapcursor_levelstart.
- Cursor moves only onto visible units with COMPLETED > 1 (mapcursor_move). Enter: mapcursor_enter(vars) -> MF_loop("enterlevel") with vars.values[1] = level unit.
- Completion: 0 invisible, 1 visible, 2 open, 3 completed (MF_savelevel/MF_savepath persist to save).
- Save: ini at sdmc:/3ds/baba/save.ini, section = world.

## Next
- Gates/map clears are reconstructed (engine.mapprogress), not taken from the original: path `gate` 1 level clears
  (`<world>_prize total`), 2 map clears (`<world>_clears total`), 3 orbs (`<world>_bonus total`), 4 clears on
  this map; a map is cleared at `unlockcount` cleared levels. The original's gate animation (gateunlock, MF_findgates,
  COMPLETED < -1 gate objects) is skipped: a met gate path is just saved open. Verify against the real game when possible.
- Orbs: MF_bonus(1/0) while playing; on win counted once per level in save `<world>_bonus` (<level>=1, total).
- Endings: MF_end / MF_allisdone save `<world> end/done = 1` (read back by the scripts), then finish like a win. No cutscenes/credits.
- Feel: moves slide over ~80ms (engine.prev + drawlist(progress)); a win holds ~1s (5 ticks) before going up.
- Not done: unlock/gate animations, particles, win animation, intro/ending cutscenes.
- Music: .ld music= track from Worlds/<w>/Music or Data/Music, streamed by source/audio.c (tremor + ndsp).
  Empty music= defaults to map/baba (a guess).
- Sound effects live in Contents/Resources/Assets.dat (Chowdren pack; header = (u32 offset, u32 size) per asset,
  sounds are assets 3206..3450 as [u32 2][u32 len][ogg]). Names: gperf table in the executable
  (739 name pointers at file offset 6890944, int32 ids at 0x61E6B0). tools/extract_sounds.py writes Data/Sounds/<name>.ogg.
- Sounds per turn: the game sets generaldata2.strings[TURNSOUND/REMOVALSOUND] (setsoundname, only while
  editor.strings[MENU] == "ingame"); engine runs checkturnsound() then plays them. Engine adds undo1-5, restart, winner2 (guessed).
- Animation: walk frame = VISUALDIR (movement.lua advances it), `animate()` turns it into unit.direction (DIR*8+VISUALDIR for tiling 2/3,
  VISUALDIR for 4). Engine calls animate() after turns and on a ~200ms timer; all sprites also wobble through _1/_2/_3 files on that timer.
- Object overrides: per changed object MF_create + addchange + MF_cleanremove before units; addunit applies (dochanges). Sprite = changes[class].image/root.

## Levels (`Worlds/<world>/NNlevel.l` + `.ld`), parser: `tools/baba_level.py`, verified on all 600 levels
- `ACHTUNG!`, u16 version (261), `MAP ` chunk (u32 size + data), `LAYR` chunk: u32 size, u16 layer count.
- Per layer: u32 w, u32 h, header (offsets/zoom floats, not decoded), then
  - `MAIN` u32 size + zlib → w*h u16 tile ids (0xFFFF = empty)
  - optional `DATA` u8 count, then per entry u32 id + u32 size + zlib → w*h bytes (id 0 = facing direction). Last layer can omit it.
- Tile id = `tile[0] + tile[1]*256` from `tileslist` in `values.lua` (156 base objects). Id 0 = level edge.
- `.ld` is ini: `[general]` name/palette/music, `[specials]`, plus per-level object overrides (not yet handled → names may be wrong in levels that redefine objects).
- Common sizes: 26x20, 30x18, 35x20, 26x16, 20x20. At 24px a 35x20 level is 840x480 → ~11px tiles on the 400x240 top screen, or scroll.

## SD layout (tools/prepare_sd.py)
- Data copied as-is except */Sprites/*.png, which go into Data/sprites.pak ("BPK1", u32 count,
  count x (u32 off, u32 size, u16 len, name) sorted, then PNG bytes); render.c reads it, falls back to loose files.
- 4376 sprites = ~1 MB of PNGs, but ~140 MB of SD space as separate files with 32 KB clusters.

## Assets
- Sprites: `Sprites/<name>_<dir/frame>_<anim>.png`, 24x24 RGB, 3983 files. Recoloured at runtime from palette.
- Palettes: `Palettes/*.png`, 7x5 grid; objects reference `colour = {x, y}`.
- Music/Sounds: `.ogg`.

## Next steps
1. 3DS skeleton (devkitARM + libctru + citro2d) with Lua 5.3.4 embedded, reading `Data/` from SD card (`sdmc:/3ds/baba/Data`).
2. Render a level natively from the parsed `.l` + sprites + palette, no Lua yet (proves the asset path).
3. Implement `mmf.newObject` + the most-called `MF_*` as stubs that log, run `load.lua`/`rules.lua`, iterate until the first level plays.

## Real hardware (New 3DS, measured 2026-10-13)
- Emulator (Azahar) timing is not representative for SD access: on hardware each file open is ~140ms in big
  folders, and closing a newly created/truncated file took 5.4s (allocation). Keep SD work rare:
  one read per file (no line-by-line IO), level names from Data/levelnames.ini, saves rewritten in place.
- Saves: two fixed-size slots (save.ini.a/.b) rewritten in place: ~20ms vs 5.4s; first creation of a slot is slow once.
- osSetSpeedupEnable(true): New 3DS 804MHz mode, ~2x on script loading. Lua is ~200x slower than a Mac.
- Turn cost is ~90% the game's own Lua (hasfeature, dynamictile). Levels with dozens of YOU units are heavy.
- Startup after all fixes: ~1.3s scripts + ~0.8s level (was ~27s).
- Animation must follow the clock (osGetTime), not loop passes: hardware frames take ~22ms.
