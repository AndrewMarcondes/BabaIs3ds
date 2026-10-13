-- Engine side of the Lua boundary (what Chowdren/Fusion provided). Game objects are plain tables.
-- MF_* behaviour follows Data/MF_documentation.txt from the game.

engine = { objects = {}, missing = {}, nextid = 100, levelserial = 0, message = "", ini = {}, blocked = {}, prev = {} }

local SYSTEM_IDS = 100 -- ids below this are engine/system objects (generaldata etc.) and survive level changes
local VARS_ID = 9      -- scratch object handed to mapcursor_enter

-- The game forces full collections on every level change (restoredefaults etc.); on the 3DS a full sweep of the
-- whole script heap is a noticeable pause, so do an incremental step instead. Lua still collects as usual.
local gc = collectgarbage
function collectgarbage(opt, ...)
	if opt == nil or opt == "collect" then return gc("step", 0) end
	return gc(opt, ...)
end

-- Fusion alterables default to 0 / "" / false.
local function defaults(v)
	return setmetatable({}, { __index = function() return v end })
end

local function newobj(id)
	return { fixed = id, values = defaults(0), strings = defaults(""), flags = defaults(false),
		className = "", direction = 0, x = 0, y = 0, visible = true }
end

mmf = {}
function mmf.newObject(id)
	local o = engine.objects[id]
	if o == nil then
		o = newobj(id)
		engine.objects[id] = o
	end
	return o
end

-- map[0]:get_x(x,y): 255 = empty, anything else is the level edge (an obstacle).
engine.edge = {}
TileMap = {}
function TileMap.new(id)
	return { [0] = { get_x = function(self, x, y) return engine.edge[x + y * roomsizex] or 255 end } }
end

---------------------------------------------------------------- storage (MF_read / MF_store)

function engine.readini(path)
	local t, sec = {}, nil
	local f = io.open(path, "rb")
	if not f then return t end
	local text = f:read("a") -- one read: map .ld files are large and line-by-line IO is slow on the SD card
	f:close()
	for line in text:gmatch("[^\r\n]+") do
		local s = line:match("^%[(.*)%]$")
		if s then
			sec = s
			t[sec] = t[sec] or {}
		elseif sec then
			local k, v = line:match("^([^=]+)=(.*)$")
			if k then t[sec][k] = v end
		end
	end
	return t
end

-- Written to path.tmp then renamed, so a power-off mid-write can't leave a half-written save.
local function writeini(path, t)
	local tmp = path .. ".tmp"
	local f = io.open(tmp, "w")
	if not f then return end
	local secs = {}
	for s in pairs(t) do secs[#secs + 1] = s end
	table.sort(secs)
	for _, s in ipairs(secs) do
		f:write("[", s, "]\n")
		local keys = {}
		for k in pairs(t[s]) do keys[#keys + 1] = k end
		table.sort(keys)
		for _, k in ipairs(keys) do f:write(k, "=", t[s][k], "\n") end
	end
	if not f:close() then return end
	os.remove(path) -- FAT (SD card) rename won't replace an existing file
	os.rename(tmp, path)
end

-- storage: level (current .ld), world (world_data.txt), save, settings, lang
function MF_read(storage, group, item)
	local t = engine.ini[storage]
	local v = t and t[group] and t[group][item]
	return v ~= nil and tostring(v) or ""
end

function MF_store(storage, group, item, value)
	local t = engine.ini[storage]
	if not t then
		t = {}
		engine.ini[storage] = t
	end
	t[group] = t[group] or {}
	t[group][item] = tostring(value)
	if storage == "save" and engine.savefile then writeini(engine.savefile, t) end
end

function MF_setfile(storage, path) end -- files are chosen by the engine (gotolevel/boot)

local function savenum(group, key) return tonumber(MF_read("save", group, key)) or 0 end

---------------------------------------------------------------- sound

-- Sounds to play this frame, by file name in Data/Sounds (taken by C after each input via engine.takesounds).
engine.sounds = {}

function MF_playsound(name) if name and name ~= "" then engine.sounds[#engine.sounds + 1] = name end end
function MF_playsound_freq(name) MF_playsound(name) end
function MF_playsound_channel(name) MF_playsound(name) end
function MF_playsound_musicvolume(name) MF_playsound(name) end
function MF_playsound_full(name) MF_playsound(name) end
-- shortcut: sounds always play to the end; loops (MF_playsound_repeat) play once and MF_stopsound is ignored
function MF_playsound_repeat(name) MF_playsound(name) end

-- The game names the turn's movement/removal sounds in generaldata2 strings; the engine plays them after the turn.
local function turnsounds()
	checkturnsound()
	for _, k in ipairs({ TURNSOUND, REMOVALSOUND }) do
		local name = generaldata2.strings[k]
		if name ~= "silent" then MF_playsound(name) end
		generaldata2.strings[k] = ""
	end
end

function engine.takesounds()
	local t = engine.sounds
	engine.sounds = {}
	return t
end

---------------------------------------------------------------- objects

function MF_emptycreate(class, x, y)
	local id = engine.nextid
	engine.nextid = id + 1
	local o = mmf.newObject(id)
	o.className = class
	o.values[XPOS], o.values[YPOS] = x, y
	o.values[ID] = -1
	if class == "path" or class == "level" then o.strings[UNITNAME] = class end
	-- a path's LEVELFILE is the level it sits in (save key "<level>,x,y"; hiddendata skips paths without it)
	if class == "path" then o.strings[U_LEVELFILE] = generaldata.strings[CURRLEVEL] end
	return id
end

function MF_create(class)
	local id = MF_emptycreate(class, 0, 0)
	if class == "Ingame_blocked" then engine.blocked[id] = true end -- the X over text of a disabled rule
	return id
end

-- Removes the X for one rule word (its unit id is in the X's TYPE), or all of them for 0.
function MF_removeblockeffect(textid)
	for id in pairs(engine.blocked) do
		local o = engine.objects[id]
		if textid == 0 or not o or o.values[TYPE] == textid then
			engine.blocked[id] = nil
			engine.objects[id] = nil
		end
	end
end

-- shortcut: effects/particles are created but never drawn; render them when visuals matter
MF_effectcreate = MF_create
MF_specialcreate = MF_create

function MF_remove(id) engine.objects[id] = nil end
MF_cleanremove = MF_remove

function MF_findunit_fixed(id) return engine.objects[id] ~= nil end

function MF_getunits()
	local t = {}
	for i, u in ipairs(units) do t[i] = u.fixed end
	return t
end

-- "a,b,c" -> {"a","b","c"}; empty string -> {}
function MF_parsestring(s)
	local t = {}
	for v in string.gmatch(s or "", "([^,]+)") do t[#t + 1] = v end
	return t
end

function MF_setcolour(id, c1, c2)
	local o = engine.objects[id]
	if o then o.colour = { tonumber(c1), tonumber(c2) } end
end

-- Level icons are coloured from their COLOUR string, or CLEARCOLOUR once completed.
function MF_updatelevelcolour(id)
	local o = engine.objects[id]
	if not o then return end
	local s = o.strings[COLOUR]
	if o.values[COMPLETED] == 3 and #o.strings[CLEARCOLOUR] > 0 then s = o.strings[CLEARCOLOUR] end
	local c = MF_parsestring(#s > 0 and s or "1,2")
	MF_setcolour(id, c[1], c[2])
end

---------------------------------------------------------------- map

function MF_findlevelunit(name)
	local t = {}
	for _, u in ipairs(units) do
		if u.strings[U_LEVELFILE] == name then t[#t + 1] = u.fixed end
	end
	return t
end

function MF_findpaths(x, y)
	local t = {}
	for _, id in ipairs(paths) do
		local p = engine.objects[id]
		if p and p.values[XPOS] == x and p.values[YPOS] == y then t[#t + 1] = id end
	end
	return t
end

-- shortcut: gates (paths that need N clears) are treated as plain paths; implement when a gate blocks progress
function MF_findgates() return {} end

-- completion: 0 invisible, 1 visible, 2 open, 3 completed
function MF_savelevel(id, completion)
	local o = engine.objects[id]
	if not o then return end
	o.values[COMPLETED] = completion
	MF_store("save", generaldata.strings[WORLD], o.strings[U_LEVELFILE], completion)
	MF_updatelevelcolour(id)
end

function MF_savepath(id, completion)
	local o = engine.objects[id]
	if not o then return end
	o.values[COMPLETED] = completion
	local key = generaldata.strings[CURRLEVEL] .. "," .. o.values[XPOS] .. "," .. o.values[YPOS]
	MF_store("save", generaldata.strings[WORLD], key, completion)
end

-- mapcursor_enter puts the chosen level unit in vars.values[1] and asks for this loop.
function MF_loop(name, n)
	if name == "enterlevel" then engine.entering = mmf.newObject(VARS_ID).values[1] end
end

---------------------------------------------------------------- levels

-- Only [general] name= is needed, and it comes first in .ld files: stop reading there (map loads read dozens).
local levelnames = {}
local function levelname(world, file)
	local key = world .. "/" .. file
	if levelnames[key] == nil then
		levelnames[key] = file
		local f = io.open(engine.data .. "/Worlds/" .. key .. ".ld", "r")
		if f then
			local sec
			for line in f:lines() do
				sec = line:match("^%[(.-)%]") or sec
				local v = sec == "general" and line:match("^name=(.-)\r?$")
				if v then levelnames[key] = v break end
				if sec and sec ~= "general" then break end
			end
			f:close()
		end
	end
	return levelnames[key]
end

function engine.ismap()
	local g = engine.ini.level and engine.ini.level.general
	return g ~= nil and g.leveltype == "1"
end

-- Level icons are not tiles: the engine makes one unit per [levels] entry of the .ld.
local function addlevelicons()
	local lv = engine.ini.level.levels or {}
	local world = generaldata.strings[WORLD]
	-- only the first [general] levels= entries are live; .ld files keep stale entries past that
	local count = tonumber((engine.ini.level.general or {}).levels) or 0
	local i = 0
	while i < count and lv[i .. "file"] do
		local file = lv[i .. "file"]
		local id = MF_emptycreate("level", tonumber(lv[i .. "X"]) or 0, tonumber(lv[i .. "Y"]) or 0)
		local o = engine.objects[id]
		o.strings[NAME] = "level"
		o.strings[UNITTYPE] = "object"
		o.strings[U_LEVELFILE] = file
		o.strings[U_LEVELNAME] = (lv[i .. "name"] or "") ~= "" and lv[i .. "name"] or levelname(world, file)
		o.strings[COLOUR] = lv[i .. "colour"] or ""
		o.strings[CLEARCOLOUR] = lv[i .. "clearcolour"] or ""
		o.values[VISUALLEVEL] = tonumber(lv[i .. "number"]) or 0
		o.values[VISUALSTYLE] = tonumber(lv[i .. "style"]) or 0
		-- .ld state is left over from editor playtests; only "open" (2) counts as a default, the save decides the rest
		local default = (tonumber(lv[i .. "state"]) or 0) >= 2 and 2 or 0
		o.values[COMPLETED] = math.max(default, tonumber(MF_read("save", world, file)) or 0)
		o.values[TILING] = -1
		o.values[ZLAYER] = 15
		o.values[DIR] = 3
		o.values[ONLINE] = 1
		addunit(id, false, true)
		i = i + 1
	end
end

-- level: { w, h, layers = { { tiles = {...}, dirs = {...} }, ... } }, tiles 0-based row-major, ids = tile[1] + tile[2]*256
function engine.loadlevel(level)
	engine.current = level
	engine.won, engine.bonus, engine.finished, engine.wintimer = false, false, nil, 0
	init(1, level.w, level.h, 24, 0, 0, 2, 3, 4, 5, 6, 7, 8, 400, 240)
	generaldata2.values[ZOOM] = 1
	spritedata.values[TILEMULT] = 1
	generaldata.values[DISABLEPARTICLES] = 1
	clearunits() -- also resets the previous level's object changes (restoredefaults)
	for id in pairs(engine.objects) do
		if id >= SYSTEM_IDS then engine.objects[id] = nil end
	end
	engine.edge, engine.blocked = {}, {}

	-- per-level object overrides from .ld [tiles] changed=objectNNN,...; addunit then applies them via dochanges
	local tiles = engine.ini.level and engine.ini.level.tiles or {}
	for i = 1, math.max(tonumber(tiles.changed_count) or 1, 1) do
		for obj in string.gmatch(tiles[i == 1 and "changed" or ("changed" .. i)] or "", "object%d%d%d") do
			if tileslist[obj] then
				local id = MF_create(obj)
				addchange(id)
				MF_cleanremove(id)
			end
		end
	end

	local byid = {}
	for class, t in pairs(tileslist) do
		if t.tile then byid[t.tile[1] + t.tile[2] * 256] = class end
	end
	for _, layer in ipairs(level.layers) do
		for i, t in ipairs(layer.tiles) do
			local x, y = (i - 1) % level.w, (i - 1) // level.w
			if t == 0 then
				engine.edge[x + y * level.w] = 0
			elseif byid[t] then
				local id = MF_emptycreate(byid[t], x, y)
				local o = engine.objects[id]
				o.values[DIR] = layer.dirs and layer.dirs[i] or 0
				o.values[ONLINE] = 1
				addunit(id, false, true)
			end
		end
	end

	local g = engine.ini.level and engine.ini.level.general or {}
	addlevelicons()
	for i = 0, (tonumber(g.paths) or 0) - 1 do addpath(i) end
	for i = 0, (tonumber(g.specials) or 0) - 1 do addspecial(i) end
	loadlevelcompletion()
	dynamictiling()
	updatecolours()
	startblock()
	animate()
	if engine.ismap() then
		openlevels()
		engine.mapprogress(g)
		-- the reveal leaves new paths/levels at 1 until the engine's unlock animation ends; we have none, so apply the saved state now
		loadlevelcompletion()
		mapcursor_load()
		mapcursor_levelstart()
	end
end

-- Cleared level icons on this map, not counting the way back to the parent map.
local function mapclears()
	local parent, n = leveltree[#leveltree - 1], 0
	for _, u in ipairs(units) do
		if u.className == "level" and u.values[COMPLETED] == 3 and u.strings[U_LEVELFILE] ~= parent then n = n + 1 end
	end
	return n
end

-- Map progress the original engine handles outside the Lua (reconstructed from the data, see NOTES.md):
-- a map is cleared once unlockcount of its levels are, and a revealed gate path opens when its count
-- (1 level clears, 2 map clears, 3 orbs, 4 clears on this map) reaches its requirement.
function engine.mapprogress(g)
	local world, here = generaldata.strings[WORLD], generaldata.strings[CURRLEVEL]
	local need = tonumber(g.unlockcount) or 0
	if need > 0 and savenum(world, here) < 3 and mapclears() >= need then
		MF_store("save", world, here, 3)
		MF_store("save", world .. "_clears", "total", savenum(world .. "_clears", "total") + 1)
	end
	local counts = { savenum(world .. "_prize", "total"), savenum(world .. "_clears", "total"),
		savenum(world .. "_bonus", "total"), mapclears() }
	for _, id in ipairs(paths) do
		local p = engine.objects[id]
		local gate = p.values[PATH_GATE]
		if gate > 0 and p.values[COMPLETED] == 1 and (counts[gate] or 0) >= p.values[PATH_REQUIREMENT] then
			MF_savepath(id, 3)
			unlocklevels(p.values[XPOS], p.values[YPOS], nil, true)
			hiddendata(p.values[XPOS], p.values[YPOS], true)
		end
	end
end

local function levelbase(name) return engine.data .. "/Worlds/" .. generaldata.strings[WORLD] .. "/" .. name end

-- Checked before the level tree changes, so a missing file can't leave CURRLEVEL pointing at it.
local function levelexists(name)
	local f = name and name ~= "" and io.open(levelbase(name) .. ".l", "rb")
	if not f then
		engine.message = "cannot load " .. tostring(name)
		return false
	end
	f:close()
	return true
end

-- Loads Worlds/<world>/<name>.l/.ld and makes it the current level.
function engine.gotolevel(name)
	local base = levelbase(name)
	local level = engine_readlevel(base .. ".l")
	if not level then
		engine.message = "cannot load " .. name
		return false
	end
	engine.ini.level = engine.readini(base .. ".ld")
	local g = engine.ini.level.general or {}
	engine.world = generaldata.strings[WORLD]
	engine.palette = (g.palette or "") ~= "" and g.palette or "default.png"
	-- shortcut: the default for an empty music= (map theme on maps, main theme in levels) is a guess at the original
	engine.music = (g.music or "") ~= "" and g.music or (g.leveltype == "1" and "map" or "baba")
	engine.roomw, engine.roomh = level.w, level.h
	local im = engine.ini.level.images or {}
	engine.images = {}
	for i = 0, (tonumber(im.total) or 0) - 1 do engine.images[#engine.images + 1] = im[tostring(i)] end
	if name ~= engine.shown then -- tells the renderer to reload; a restart keeps its sprite cache
		engine.shown = name
		engine.levelserial = engine.levelserial + 1
	end
	generaldata.strings[CURRLEVEL] = name
	editor.strings[MENU] = "ingame" -- the game's turn sounds only play in this menu state
	engine.loadlevel(level)
	engine.message = g.name or name
	return true
end

-- Starts the game the way the original does: firstlevel() picks level 1 or, once it's won, the map.
function engine.boot()
	init(1, 1, 1, 24, 0, 0, 2, 3, 4, 5, 6, 7, 8, 400, 240)
	setupmenu(10, 11, 12, 13, 14, 15, true) -- editor objects; map code reads editor.values
	generaldata.strings[WORLD] = "baba"
	generaldata.strings[BASEWORLD] = "baba"
	generaldata.strings[CURRLEVEL] = ""
	engine.ini.world = engine.readini(engine.data .. "/Worlds/baba/world_data.txt")
	if engine.savefile then
		local f = io.open(engine.savefile, "r")
		if f then f:close() else os.rename(engine.savefile .. ".tmp", engine.savefile) end -- finish an interrupted save
	end
	engine.ini.save = engine.savefile and engine.readini(engine.savefile) or {}
	firstlevel()
	-- shortcut: the real engine restores the level tree from the save; we only know the start map
	if #leveltree == 0 then sublevel(generaldata.strings[CURRLEVEL], 0, 0) end
	return engine.gotolevel(generaldata.strings[CURRLEVEL])
end

-- shortcut: restart is a plain reload, so it can't be undone like in the real game
function MF_restart(instant)
	MF_playsound("restart")
	engine.gotolevel(generaldata.strings[CURRLEVEL])
end

-- block() calls this when a YOU unit is on a WIN unit.
-- shortcut: no win animation/sound; we save, then go up to the map at the end of the turn
-- The game calls this when YOU touches BONUS (and with 0 when that's undone); it counts once the level is won.
function MF_bonus(status) engine.bonus = status == 1 end

function MF_win()
	if engine.won then return end -- several win paths can fire in one turn
	local world, level = generaldata.strings[WORLD], generaldata.strings[CURRLEVEL]
	if savenum(world, level) < 3 then
		MF_store("save", world .. "_prize", "total", savenum(world .. "_prize", "total") + 1)
	end
	if engine.bonus and savenum(world .. "_bonus", level) == 0 then
		MF_store("save", world .. "_bonus", level, 1)
		MF_store("save", world .. "_bonus", "total", savenum(world .. "_bonus", "total") + 1)
	end
	MF_store("save", world, level, 3)
	MF_playsound("winner2")
	engine.won = true
end

-- Endings: the original plays a cutscene and credits. We record what the scripts read back later
-- (save "end"/"done" flags unlock post-game content), then finish like a win with a message.
-- shortcut: no ending cutscenes or credits
local function finish(flag, text)
	MF_store("save", generaldata.strings[WORLD], flag, 1)
	MF_win()
	engine.finished = text
end
function MF_end() finish("end", "The End") end
function MF_end_single() MF_store("save", generaldata.strings[WORLD], "end", 1) end
function MF_allisdone() finish("done", "All is done") end
function MF_done_single() MF_store("save", generaldata.strings[WORLD], "done", 1) end
function MF_credits() end

local function goup()
	if #leveltree <= 1 or not levelexists(leveltree[#leveltree - 1]) then return false end
	uplevel()
	return engine.gotolevel(generaldata.strings[CURRLEVEL])
end

-- One player input per turn: right/up/left/down/idle/undo/restart from the game's `keys` table,
-- plus "enter" (enter level on a map, otherwise wait) and "leave" (back to the map).
function engine.input(key)
	engine.prev = {} -- where units were before this turn, for the slide animation
	for _, u in ipairs(units) do engine.prev[u.fixed] = { u.values[XPOS], u.values[YPOS] } end
	if key == "leave" then
		goup()
		return
	end
	if engine.won and key ~= "restart" then return end
	if key == "enter" then
		if engine.ismap() then
			mapcursor_enter(VARS_ID)
		else
			key = "idle"
		end
	end
	if key == "undo" then
		if undo() == 1 then MF_playsound("undo" .. math.random(1, 5)) end
		updatecode = 1
		code()
	elseif key ~= "enter" then
		command(key)
		block()
		if key ~= "restart" then turnsounds() end
		newundo()
	end

	animate()

	if engine.entering then
		local l = engine.objects[engine.entering]
		engine.entering = nil
		if l and levelexists(l.strings[U_LEVELFILE]) then
			sublevel(l.strings[U_LEVELFILE], l.values[VISUALLEVEL], l.values[VISUALSTYLE])
			engine.gotolevel(l.strings[U_LEVELFILE])
		end
	end
end

-- After a win the level stays on screen for ~1s (5 ticks) before going back up to the map.
local WIN_TICKS = 5
local function leavewon()
	engine.wintimer = (engine.wintimer or 0) + 1
	if engine.wintimer < WIN_TICKS then return end
	engine.wintimer = 0
	local done = engine.finished or ("Cleared: " .. engine.message)
	if goup() then engine.message = done .. "\n" .. engine.message end
end

-- Animation timer (the original engine calls animate() on a timer too): cycles animated objects (tiling 3/4).
-- shortcut: animate() also re-derives walk frames, which only change on turns; fine since it is idempotent for them
function engine.tick()
	animate()
	if engine.won then leavewon() end
end

---------------------------------------------------------------- bottom screen

local W = 39 -- libctru console: 40 columns, 30 rows on the bottom screen
local function fit(t) return #t > W and t:sub(1, W - 1) .. "~" or t end

-- "baba on rock is you": target, conditions ("on rock", "lonely"), verb, effect
local function ruletext(r)
	local words = { r[1][1] }
	for _, c in ipairs(r[2] or {}) do
		local params = type(c[2]) == "table" and #c[2] > 0 and (" " .. table.concat(c[2], " and ")) or ""
		words[#words + 1] = tostring(c[1]) .. params
	end
	words[#words + 1] = r[1][2]
	words[#words + 1] = r[1][3]
	return table.concat(words, " ")
end

-- Text for the bottom screen (with console colour codes); C prints it whenever it changes.
function engine.status()
	local yellow, grey, reset = "\27[33m", "\27[37;2m", "\27[0m"
	local out = { yellow .. fit(engine.message:gsub("\n", " / ")) .. reset, "" }
	if engine.ismap() then
		local c = engine.objects[generaldata4.values[MAINCURSOR]]
		local l = c and engine.objects[c.values[CURSOR_ONLEVEL]]
		if l and l.className == "level" then out[#out + 1] = fit("> " .. l.strings[U_LEVELNAME]) end
	else
		local seen, n = {}, 0
		for _, r in ipairs(visualfeatures or {}) do
			local t = ruletext(r)
			if not seen[t] then
				seen[t] = true
				n = n + 1
				if n <= 18 then out[#out + 1] = fit(t) elseif n == 19 then out[#out + 1] = "..." end
			end
		end
		if engine.won then out[#out + 1] = "" out[#out + 1] = yellow .. "Congratulations!" .. reset end
	end
	while #out < 24 do out[#out + 1] = "" end
	out[#out + 1] = grey .. "D-pad move   A wait/enter   B undo"
	out[#out + 1] = "X restart   SELECT map   START menu" .. reset
	return table.concat(out, "\n")
end

---------------------------------------------------------------- drawing

-- Units in draw order: { sprite, root, frame, x, y, c1, c2, zlayer, part }; sprite "#box" = filled tile, "#cross" = X,
-- "@file" = exact sprite file; part 1/2 = left/right half of the tile.
function engine.drawlist(progress)
	progress = progress or 1 -- 0..1 through the current move
	local list = {}
	-- positions go to C in 1/16 tiles so moving units can be drawn between tiles
	local function add(...)
		local e = { ... }
		e[4], e[5] = math.floor(e[4] * 16 + 0.5), math.floor(e[5] * 16 + 0.5)
		list[#list + 1] = e
	end

	-- open paths: the path object's sprite, joined to neighbouring paths/levels when it tiles
	local open = {}
	for _, id in ipairs(paths) do
		local p = engine.objects[id]
		if p and p.values[COMPLETED] >= 1 then open[p.values[XPOS] + p.values[YPOS] * roomsizex] = p end
	end
	for _, u in ipairs(units) do
		if u.className == "level" and u.values[COMPLETED] >= 1 then open[u.values[XPOS] + u.values[YPOS] * roomsizex] = u end
	end
	for _, p in pairs(open) do
		local t = tileslist[p.strings[PATH_OBJECT]]
		if p.className == "path" and p.values[PATH_GATE] > 0 and p.values[COMPLETED] < 2 then
			t = tileslist[p.strings[PATH_GATEOBJECT]] or t -- still-locked gate
		end
		if p.className == "path" and t then
			local x, y, frame = p.values[XPOS], p.values[YPOS], 0
			if t.tiling == 1 then
				for d, v in ipairs({ { 1, 0 }, { 0, -1 }, { -1, 0 }, { 0, 1 } }) do
					if open[x + v[1] + (y + v[2]) * roomsizex] then frame = frame | (1 << (d - 1)) end
				end
			end
			local c = t.colour or { 0, 3 }
			add(t.sprite, t.sprite_in_root ~= false, frame, x, y, c[1], c[2], 1)
		end
	end

	for _, u in ipairs(units) do
		local t = tileslist[u.className]
		if u.className == "level" then
			if u.values[COMPLETED] >= 1 then
				local c = u.colour or { 1, 2 }
				local x, y, n = u.values[XPOS], u.values[YPOS], u.values[VISUALLEVEL]
				add("#box", true, 0, x, y, c[1], c[2], u.values[ZLAYER])
				-- style 0 = number, 1 = letter, 2 = dot, -1 = [icons] entry <number>
				local icons = engine.ini.level.icons or {}
				if u.values[VISUALSTYLE] == -1 and icons[n .. "file"] then
					list[#list] = nil -- icons replace the box
					add("@" .. icons[n .. "file"], icons[n .. "root"] == "1", 0, x, y, c[1], c[2], u.values[ZLAYER])
				elseif u.values[VISUALSTYLE] == 0 and n < 10 then
					add("text_" .. n, true, 0, x, y, 0, 4, u.values[ZLAYER])
				elseif u.values[VISUALSTYLE] == 0 and n < 100 then -- two digits, each squeezed into half the tile
					add("text_" .. n // 10, true, 0, x, y, 0, 4, u.values[ZLAYER], 1)
					add("text_" .. n % 10, true, 0, x, y, 0, 4, u.values[ZLAYER], 2)
				elseif u.values[VISUALSTYLE] == 1 and n < 26 then
					add("text_" .. string.char(97 + n), true, 0, x, y, 0, 4, u.values[ZLAYER])
				elseif u.values[VISUALSTYLE] == 2 then
					add("dot", true, 0, x, y, 0, 4, u.values[ZLAYER])
				end
			end
		elseif t and u.visible then
			local tiling = u.values[TILING]
			local frame = 0
			-- tiling 1 (dynamictiling) and 2/3/4 (animate) keep their sprite frame in unit.direction
			if tiling == 1 or tiling >= 2 then frame = u.direction
			elseif tiling == 0 then frame = u.values[DIR] * 8 end
			local c = u.colour or t.colour or { 0, 3 }
			local ch = changes[u.className] or {}
			local root = t.sprite_in_root ~= false
			if ch.root ~= nil then root = ch.root end
			local x, y, p = u.values[XPOS], u.values[YPOS], engine.prev[u.fixed]
			if p and math.abs(x - p[1]) <= 1 and math.abs(y - p[2]) <= 1 then -- slide one-tile moves; jumps snap
				x, y = p[1] + (x - p[1]) * progress, p[2] + (y - p[2]) * progress
			end
			add(ch.image or t.sprite or u.className, root, frame, x, y, c[1], c[2], u.values[ZLAYER])
		end
	end
	for id in pairs(engine.blocked) do
		local o = engine.objects[id]
		local c = o and o.colour or { 2, 2 }
		if o and o.visible then add("#cross", true, 0, o.values[XPOS], o.values[YPOS], c[1], c[2], 20) end
	end
	-- stable by layer, then insertion order
	for i, e in ipairs(list) do e.order = i end
	table.sort(list, function(a, b) if a[8] ~= b[8] then return a[8] < b[8] end return a.order < b.order end)
	return list
end

-- Any MF_* not defined yet becomes a stub that counts its calls.
setmetatable(_G, { __index = function(t, k)
	if type(k) == "string" and k:sub(1, 3) == "MF_" then
		local f = function(...)
			engine.missing[k] = (engine.missing[k] or 0) + 1
		end
		rawset(t, k, f)
		return f
	end
end })
