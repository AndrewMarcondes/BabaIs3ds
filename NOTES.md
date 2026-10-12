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
- Next: win (MF_win), walk animation frames, level switching/map, .ld object overrides, sound.

## Levels (`Worlds/<world>/NNlevel.l` + `.ld`), parser: `tools/baba_level.py`, verified on all 600 levels
- `ACHTUNG!`, u16 version (261), `MAP ` chunk (u32 size + data), `LAYR` chunk: u32 size, u16 layer count.
- Per layer: u32 w, u32 h, header (offsets/zoom floats, not decoded), then
  - `MAIN` u32 size + zlib → w*h u16 tile ids (0xFFFF = empty)
  - optional `DATA` u8 count, then per entry u32 id + u32 size + zlib → w*h bytes (id 0 = facing direction). Last layer can omit it.
- Tile id = `tile[0] + tile[1]*256` from `tileslist` in `values.lua` (156 base objects). Id 0 = level edge.
- `.ld` is ini: `[general]` name/palette/music, `[specials]`, plus per-level object overrides (not yet handled → names may be wrong in levels that redefine objects).
- Common sizes: 26x20, 30x18, 35x20, 26x16, 20x20. At 24px a 35x20 level is 840x480 → ~11px tiles on the 400x240 top screen, or scroll.

## Assets
- Sprites: `Sprites/<name>_<dir/frame>_<anim>.png`, 24x24 RGB, 3983 files. Recoloured at runtime from palette.
- Palettes: `Palettes/*.png`, 7x5 grid; objects reference `colour = {x, y}`.
- Music/Sounds: `.ogg`.

## Next steps
1. 3DS skeleton (devkitARM + libctru + citro2d) with Lua 5.3.4 embedded, reading `Data/` from SD card (`sdmc:/3ds/baba/Data`).
2. Render a level natively from the parsed `.l` + sprites + palette, no Lua yet (proves the asset path).
3. Implement `mmf.newObject` + the most-called `MF_*` as stubs that log, run `load.lua`/`rules.lua`, iterate until the first level plays.
