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
	local f = io.open(path, "rb")
	if not f then return {} end
	local text = f:read("a") -- one read: map .ld files are large and line-by-line IO is slow on the SD card
	f:close()
	return engine.parseini(text)
end

function engine.parseini(text)
	local t, sec = {}, nil
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

-- The save lives in two slots, <savefile>.a and .b, overwritten in place: "#gen=N", the ini text, "#end", padding.
-- Each write goes to the older slot and loading takes the newest slot with its #end, so a power-off mid-write
-- loses at most that write. In place matters on the 3DS: closing a newly created/truncated file took ~5s there
-- (the SD card has to allocate space); rewriting an existing file of the same size doesn't allocate.
local SLOT_MIN = 16384

local function slotpath(gen) return engine.savefile .. (gen % 2 == 1 and ".a" or ".b") end

local function readslot(path)
	local f = io.open(path, "rb")
	if not f then return nil end
	local text = f:read("a")
	f:close()
	local gen, body = text:match("^#gen=(%d+)\n(.-)#end\n")
	if gen then return tonumber(gen), body end
end

-- Newest complete slot, or an old single-file save.ini from earlier builds.
function engine.loadsave()
	local ga, a = readslot(slotpath(1))
	local gb, b = readslot(slotpath(2))
	if ga or gb then
		local gen, body = ga, a
		if gb and (not ga or gb > ga) then gen, body = gb, b end
		engine.savegen, engine.saved = gen, body
		return engine.parseini(body)
	end
	engine.savegen, engine.saved = 0, nil
	return engine.readini(engine.savefile)
end

local function initext(t)
	local out, secs = {}, {}
	for s in pairs(t) do secs[#secs + 1] = s end
	table.sort(secs)
	for _, s in ipairs(secs) do
		out[#out + 1] = "[" .. s .. "]"
		local keys = {}
		for k in pairs(t[s]) do keys[#keys + 1] = k end
		table.sort(keys)
		for _, k in ipairs(keys) do out[#out + 1] = k .. "=" .. t[s][k] end
	end
	return table.concat(out, "\n") .. "\n"
end

local function writesave(t)
	local text = initext(t)
	if text == engine.saved then return end -- unchanged since the last write
	local gen = (engine.savegen or 0) + 1
	local data = "#gen=" .. gen .. "\n" .. text .. "#end\n"
	local path = slotpath(gen)
	local f = io.open(path, "r+b")
	local size = f and f:seek("end") or 0
	if size < #data then -- first save, or the save outgrew its slot: allocating is slow, so leave room to grow
		if f then f:close() end
		size = math.max(SLOT_MIN, #data * 2)
		f = io.open(path, "wb")
		if not f then return end
	end
	f:setvbuf("full", size) -- one write to the card, not one per 1KB buffer
	f:seek("set", 0)
	f:write(data, string.rep(" ", size - #data))
	if f:close() then engine.savegen, engine.saved = gen, text end
end

-- Saves are written at most once per turn (engine.flushsave), not on every MF_store.
function engine.flushsave()
	if engine.savedirty and engine.savefile then writesave(engine.ini.save) end
	engine.savedirty = false
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
	if storage == "save" then engine.savedirty = true end
end

function MF_setfile(storage, path) end -- files are chosen by the engine (gotolevel/boot)

local function savenum(group, key) return tonumber(MF_read("save", group, key)) or 0 end

---------------------------------------------------------------- files (mods)

-- The game names files relative to the PC install ("Data/Lua/x.lua"); ours live under engine.data.
local function datapath(p)
	if type(p) == "string" and p:sub(1, 5) == "Data/" then return engine.data .. p:sub(5) end
	return p
end

local _dofile = dofile
function dofile(p) return _dofile(datapath(p)) end

-- MF_filelist("Data/Lua/", "*.lua") -> file names in that folder matching the pattern ("*.*" = all)
function MF_filelist(path, pattern)
	local ext = (pattern or "*.*"):match("^%*(%..*)$")
	local t = {}
	for _, name in ipairs(engine_listdir(datapath(path)) or {}) do
		if ext == ".*" or (ext and name:sub(-#ext) == ext) then t[#t + 1] = name end
	end
	return t
end

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
local levelnames
local function levelname(world, file)
	local key = world .. "/" .. file
	if levelnames == nil then
		-- Data/levelnames.ini (written by tools/prepare_sd.py) has every name in one file: opening dozens of
		-- .ld files per map load is slow on the 3DS SD card. Without it, fall back to reading each .ld.
		levelnames = {}
		for w, names in pairs(engine.readini(engine.data .. "/levelnames.ini")) do
			for f, n in pairs(names) do levelnames[w .. "/" .. f] = n end
		end
	end
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
	findplayer() -- the original engine calls this after every turn: sets the "no YOU" music state
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
	-- paletteroot=0: the pack brings its own palette in Worlds/<pack>/Palettes
	local pal = (g.palette or "") ~= "" and g.palette or "default.png"
	engine.palette = (g.paletteroot == "0" and ("Worlds/" .. engine.world .. "/Palettes/") or "Palettes/") .. pal
	-- shortcut: the default for an empty music= (map theme on maps, main theme in levels) is a guess at the original
	engine.leveltrack = (g.music or "") ~= "" and g.music or (g.leveltype == "1" and "map" or "baba")
	engine.music = engine.leveltrack
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
	engine.cardname = engine.ismap() and "" or (g.name or name) -- shown on the level start card (C)
	return true
end

---------------------------------------------------------------- speedups

-- The game's hasfeature(rule1, rule2, rule3, ...) is called ~2000 times on a turn with many YOU units (~25% of the
-- turn, NOTES.md), and most of its cost per call is overhead: string.sub on every call, ipairs. This copy does the same
-- four passes over the same featureindex lists in the same order, testcond included, without that overhead (~40%
-- faster). Calls without rule3 ("is there any rule1 rule2 ...") go to the game's own function.
-- test/speedups_test.lua plays every level both ways and compares the game state.
local function fast_hasfeature(game)
	local istext = setmetatable({}, { __index = function(t, k)
		local v = string.sub(k, 1, 5) == "text_"
		t[k] = v
		return v
	end })

	local function pass(l, r1, r2, r3, unitid, x, y, checkedconds, ignorebroken)
		for i = 1, #l do
			local rules = l[i]
			local conds = rules[2]
			if conds[1] ~= "never" then
				local rule = rules[1]
				if rule[1] == r1 and rule[2] == r2 and rule[3] == r3
					and testcond(conds, unitid, x, y, nil, nil, checkedconds, ignorebroken) then
					return true
				end
			end
		end
		return false
	end

	return function(rule1, rule2, rule3, unitid, x, y, checkedconds, ignorebroken_)
		if rule1 == nil or rule2 == nil or rule3 == nil then
			return game(rule1, rule2, rule3, unitid, x, y, checkedconds, ignorebroken_)
		end
		local ignorebroken = false
		if ignorebroken_ ~= nil then ignorebroken = ignorebroken_ end
		local fi = featureindex
		local t = fi["text"]
		local l = fi[rule1]
		if l and pass(l, rule1, rule2, rule3, unitid, x, y, checkedconds, ignorebroken) then return true end
		if t and istext[rule1] and pass(t, "text", rule2, rule3, unitid, x, y, checkedconds, ignorebroken) then return true end
		l = fi[rule3]
		if l and pass(l, rule1, rule2, rule3, unitid, x, y, checkedconds, ignorebroken) then return true end
		if t and istext[rule3] and pass(t, rule1, rule2, "text", unitid, x, y, checkedconds, ignorebroken) then return true end
		return nil
	end
end

-- The game's addundo(line, uid) builds a text of the whole line on every call for a debug message that is commented
-- out, and makes a table it then replaces; with dozens of units moving that is ~15% of a turn. This copy stores the
-- same record (the line's fields up to the first nil, newest first). With the game's event log on it calls the game's.
local function fast_addundo(game)
	return function(line, uid_)
		if doundo then
			if generaldata.flags[LOGGING] and logevents then return game(line, uid_) end
			local entry = {}
			for i, v in ipairs(line) do entry[i] = v end
			table.insert(undobuffer[1], 1, entry)
		end
	end
end

-- The game's dynamictile(unitid, x, y, name, extra) (which frame of a joined-up object like a wall to show) runs for
-- every wall next to every moving wall and makes two tables per call; ~13% of a WALL IS YOU turn. This copy checks the
-- same neighbours against the same names in the same order without them. Its frame number is a float when any
-- neighbour joins (the game adds 2 ^ n), as in the game.
local function fast_dynamictile(game)
	local bit = { 2 ^ 0, 2 ^ 1, 2 ^ 2, 2 ^ 3 }

	-- one name from the game's list {name, "edge", "level", extra...}: does it join with what's on tileid?
	local function joins(uth, d, unitid, tileid)
		if uth[d] == nil then return false end
		if d ~= "level" or generaldata.strings[WORLD] == generaldata.strings[BASEWORLD] then return true end
		if (unitid < 0) or (unitid > 2) then
			if mmf.newObject(unitid).values[COMPLETED] > 0 then return true end
		end
		local umh = unitmap[tileid]
		if umh ~= nil then
			for _, f in ipairs(umh) do
				local funit = mmf.newObject(f)
				if (funit.strings[UNITNAME] == "level") and (funit.visible and (funit.values[COMPLETED] > 0)) then return true end
			end
		end
		return false
	end

	return function(unitid, x, y, name, extra_)
		local result = 0
		local n = 4
		if (unitreference[name] ~= nil) and (specialtiling[unitreference[name]] ~= nil) and specialtiling[unitreference[name]] then
			n = 8
		end
		local sdirs = n == 8 and {} or nil -- only handlespecialtiling reads it
		local sresult, sresult2 = false, false
		for i = 1, n do
			local v = dirs_diagonals_[i]
			local tx, ty = x + v[1], y + v[2]
			local tileid = tx + ty * roomsizex
			local uth = unittypeshere[tileid]
			local found = false
			if (uth ~= nil) and inbounds(tx, ty) then
				found = joins(uth, name, unitid, tileid) or joins(uth, "edge", unitid, tileid) or joins(uth, "level", unitid, tileid)
				if not found and extra_ ~= nil then
					for _, d in ipairs(extra_) do
						if joins(uth, d, unitid, tileid) then found = true; break end
					end
				end
			end
			local edge = (tx == 0) or (ty == 0) or (tx == roomsizex - 1) or (ty == roomsizey - 1)
			if sdirs then sdirs[i] = 0 end
			if found or edge then
				if sdirs then sdirs[i] = 1 end
				if i < 5 then result = result + bit[i] else sresult = true end
			end
		end
		if sresult then
			result = handlespecialtiling(sdirs, result)
			if result > 31 then
				sresult2 = true
				result = result % 32
			end
		end
		return result, sresult2
	end
end

-- The game's findfeature(rule1, rule2, rule3) copies every candidate rule into new tables and builds up to three strings
-- per candidate to compare with "rule1 rule2 rule3" (~11% of a WALL IS YOU turn). This copy takes the same candidates
-- from the same lists in the same order and makes the same string comparisons, building a string only when its length
-- can match. Calls with only rule2 ("any rule with this verb") go to the game's own function.
local function fast_findfeature(game)
	-- the game compares words[3], words[2] .. " " .. words[3] and the three words joined with the request
	local function matches(w1, w2, w3, rule, n)
		if w1 == nil or w2 == nil or w3 == nil then return false end -- the game skips rules with fewer than 3 words
		if w3 == rule then return true end
		if type(w1) ~= "string" or type(w2) ~= "string" or type(w3) ~= "string" then
			return (w2 .. " " .. w3) == rule or (w1 .. " " .. w2 .. " " .. w3) == rule
		end
		local l2, l3 = #w2, #w3
		if l2 + 1 + l3 == n and (w2 .. " " .. w3) == rule then return true end
		return #w1 + 2 + l2 + l3 == n and (w1 .. " " .. w2 .. " " .. w3) == rule
	end

	return function(rule1, rule2, rule3)
		if rule1 == nil and rule3 == nil then return game(rule1, rule2, rule3) end
		local rule = ""
		if rule1 ~= nil then rule = rule1 .. " " end
		if rule2 ~= nil then rule = rule .. rule2 .. " " end
		if rule3 ~= nil then rule = rule .. rule3 end
		local n = #rule
		local result
		local fi = featureindex
		local l1 = fi[rule1]
		if l1 ~= nil then
			for i = 1, #l1 do
				local rules = l1[i]
				local r, conds = rules[1], rules[2]
				if conds[1] ~= "never" and r[1] == rule1 and r[2] == rule2 and matches(r[1], r[2], r[3], rule, n) then
					result = result or {}
					result[#result + 1] = { r[1], conds }
				end
			end
		end
		local l3 = fi[rule3]
		if l3 ~= nil and l1 == nil then
			for i = 1, #l3 do
				local rules = l3[i]
				local r, conds = rules[1], rules[2]
				if conds[1] ~= "never" and r[3] == rule3 and r[2] == rule2 and matches(r[1], r[2], r[3], rule, n) then
					result = result or {}
					result[#result + 1] = { r[1], conds }
				end
			end
		end
		return result
	end
end

-- Replaces game functions with faster ones that behave the same. Called after the game's scripts load and before
-- mods, so a mod's own version of a function still wins. engine.game keeps the originals (for the tests).
function engine.speedups()
	if engine.game then return end
	engine.game = { hasfeature = hasfeature, addundo = addundo, dynamictile = dynamictile, findfeature = findfeature }
	engine.fast = { hasfeature = fast_hasfeature(hasfeature), addundo = fast_addundo(addundo),
		dynamictile = fast_dynamictile(dynamictile), findfeature = fast_findfeature(findfeature) }
	for name, f in pairs(engine.fast) do _G[name] = f end
end

---------------------------------------------------------------- benchmark

-- Pause menu > Benchmark: C plays each scenario's moves and undoes them several times, timing every move. The moves
-- were checked not to win (the save is also kept from being written meanwhile), and undo brings back the start.
local function wallisyou() -- level 5 with the WALL and BABA text swapped: WALL IS YOU (50 YOU units), BABA IS STOP
	local tw, tb
	for _, u in ipairs(units) do
		if u.strings[UNITNAME] == "text_wall" then tw = u end
		if u.strings[UNITNAME] == "text_baba" then tb = u end
	end
	if not (tw and tb) then return end
	local wx, wy, bx, by = tw.values[XPOS], tw.values[YPOS], tb.values[XPOS], tb.values[YPOS]
	update(tw.fixed, bx, by, tw.values[DIR])
	update(tb.fixed, wx, wy, tb.values[DIR])
	updatecode = 1
	code()
	newundo() -- the swap is its own undo step, so undoing the moves stops here
end

engine.benchmarks = {
	{ name = "level 5", level = "5level", moves = { "right", "right", "down", "down", "left", "left", "up", "up" } },
	{ name = "level 5, wall is you", level = "5level", setup = wallisyou,
		moves = { "right", "left", "up", "down", "right", "left", "up", "down" } },
	{ name = "main map", level = "106level", moves = { "right", "right", "up", "up", "left", "left", "down", "down" } },
}

local function copy(t)
	local c = {}
	for k, v in pairs(t) do c[k] = type(v) == "table" and copy(v) or v end
	return c
end

-- Before the first scenario: the save stays as it is until engine.benchend.
function engine.benchbegin()
	engine.benchkeep = { file = engine.savefile, save = engine.ini.save, level = engine.shown }
	engine.savefile = nil
	engine.ini.save = copy(engine.ini.save)
end

-- Sets up scenario i; returns its name and moves, or nil after the last.
function engine.benchsetup(i)
	local b = engine.benchmarks[i]
	if not b then return nil end
	math.randomseed(1)
	if not engine.gotolevel(b.level) then return nil end
	if b.setup then b.setup() end
	return b.name, b.moves
end

-- Puts the save back and goes back to the start of the level the player was in.
function engine.benchend()
	local k = engine.benchkeep
	engine.savefile, engine.ini.save, engine.savedirty = k.file, k.save, false
	engine.benchkeep = nil
	return engine.gotolevel(k.level)
end

-- A real full collection (collectgarbage("collect") only does a step, see the top of this file).
function engine.fullgc() gc("collect") end

-- Starts the game the way the original does: firstlevel() picks level 1 or, once it's won, the map.
function engine.boot()
	local r = engine.boot_()
	engine.flushsave()
	return r
end

function engine.boot_()
	local world = engine.world or "baba"
	init(1, 1, 1, 24, 0, 0, 2, 3, 4, 5, 6, 7, 8, 400, 240)
	setupmenu(10, 11, 12, 13, 14, 15, true) -- editor objects; map code reads editor.values
	generaldata.strings[WORLD] = world
	generaldata.strings[BASEWORLD] = "baba" -- the main game; endings/intro behave differently in other packs
	generaldata.strings[CURRLEVEL] = ""
	engine.ini.world = engine.readini(engine.data .. "/Worlds/" .. world .. "/world_data.txt")
	engine.speedups()
	-- mods, the way the original loads them: Data/Lua/*.lua always, Worlds/<pack>/Lua/*.lua for that pack
	setupmods_global()
	if world ~= "baba" then setupmods_levelpack() end
	engine.ini.save = engine.savefile and engine.loadsave() or {}
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

-- findplayer() reports whether anything is YOU: without a player the level music gives way to an ominous drone.
-- Maps have no YOU (the cursor is SELECT), so they always keep their own track.
function MF_musicstate(status)
	engine.music = status == 1 and not engine.ismap() and "noise" or engine.leveltrack
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
	engine.input_(key)
	engine.flushsave()
end

function engine.input_(key)
	-- The game marks units it creates/converts so they change only once per turn; the original engine clears
	-- the mark between turns (MF_update). Without this, something that transformed once never could again.
	for _, u in ipairs(units) do u.flags[CONVERTED] = false end
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
		findplayer(true)
	elseif key ~= "enter" then
		command(key)
		block()
		if key ~= "restart" then turnsounds() end
		newundo()
		if key ~= "restart" then findplayer() end -- restart reloads the level, which calls it
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
-- Animation timer step. Returns true if the picture may have changed: an animated object showed a new frame, or a won
-- level is left after a pause; otherwise C keeps its draw list (the sprites' wobble is drawn by C). Characters are in
-- animunits too but only change frame when they fall asleep or wake up.
function engine.tick()
	local anim, before, changed = animunits or {}, {}, engine.won
	for i, id in ipairs(anim) do
		local u = mmf.newObject(id)
		before[i] = u and u.direction
	end
	animate()
	for i, id in ipairs(anim) do
		local u = mmf.newObject(id)
		if not changed and (not u or u.direction ~= before[i]) then changed = true end
	end
	if engine.won then leavewon() end
	engine.flushsave()
	return changed
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

-- What to draw, for C: engine.drawlist() returns columns, n items with one array per field and the draw order in
-- .order (by layer, then as added). The arrays are reused from frame to frame; a new table per item per frame, and
-- sorting with a Lua comparison, were most of the cost of a frame on the 3DS.
-- Fields: sprite ("#box" = filled tile, "#cross" = X, "@file" = exact sprite file), root, frame, x, y (1/16 tiles),
-- c1, c2 (palette), layer, part (1/2 = left/right half of the tile), fx, fy (where its last move started, 1/16 tiles;
-- C slides between them), shade (percent to darken (< 0) or lighten (> 0) the colour).
local DL_FIELDS = { "sprite", "root", "frame", "x", "y", "c1", "c2", "layer", "part", "fx", "fy", "shade" }
local dl = { n = 0, order = {} }
for _, f in ipairs(DL_FIELDS) do dl[f] = {} end

-- shared constants instead of tables made every frame (only read)
local TILE_COLOUR, LEVEL_COLOUR, CROSS_COLOUR, WHITE = { 0, 3 }, { 1, 2 }, { 2, 2 }, { 0, 3 }
local NONE = {}
local NEIGHBOURS = { { 1, 0 }, { 0, -1 }, { -1, 0 }, { 0, 1 } }

local function add(sprite, root, frame, x, y, c1, c2, layer, part, fx, fy, shade)
	local i = dl.n + 1
	dl.n = i
	x, y = x * 16, y * 16
	dl.sprite[i], dl.root[i], dl.frame[i], dl.x[i], dl.y[i] = sprite, root, frame, x, y
	dl.c1[i], dl.c2[i], dl.layer[i], dl.part[i] = c1, c2, layer, part
	dl.fx[i], dl.fy[i], dl.shade[i] = fx and fx * 16 or x, fy and fy * 16 or y, shade
end

-- Cleared map levels, as shades of each level's own colour so they follow the map's theme
local LEVEL_CLEARED = -35       -- the tile
local LEVEL_CLEARED_MARK = -70  -- its number, letter or dot

function engine.drawlist()
	dl.n = 0

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
				for d, v in ipairs(NEIGHBOURS) do
					if open[x + v[1] + (y + v[2]) * roomsizex] then frame = frame | (1 << (d - 1)) end
				end
			end
			local c = t.colour or TILE_COLOUR
			add(t.sprite, t.sprite_in_root ~= false, frame, x, y, c[1], c[2], 1)
		end
	end

	for _, u in ipairs(units) do
		local t = tileslist[u.className]
		if u.className == "level" then
			if u.values[COMPLETED] >= 1 then
				local c = u.colour or LEVEL_COLOUR
				local x, y, n = u.values[XPOS], u.values[YPOS], u.values[VISUALLEVEL]
				-- a level still to clear is its colour with a white mark; a cleared one a darker shade of it with the
				-- mark darker still (the game's clear colour is often the same as the uncleared one)
				local cleared = u.values[COMPLETED] == 3
				local m, ms = WHITE, 0
				if cleared then m, ms = c, LEVEL_CLEARED_MARK end
				add("#box", true, 0, x, y, c[1], c[2], u.values[ZLAYER], nil, nil, nil, cleared and LEVEL_CLEARED or 0)
				-- style 0 = number, 1 = letter, 2 = dot, -1 = [icons] entry <number>
				local icons = engine.ini.level.icons or NONE
				if u.values[VISUALSTYLE] == -1 and icons[n .. "file"] then
					dl.n = dl.n - 1 -- icons replace the box
					add("@" .. icons[n .. "file"], icons[n .. "root"] == "1", 0, x, y, c[1], c[2], u.values[ZLAYER])
				elseif u.values[VISUALSTYLE] == 0 and n < 10 then
					add("text_" .. n, true, 0, x, y, m[1], m[2], u.values[ZLAYER], nil, nil, nil, ms)
				elseif u.values[VISUALSTYLE] == 0 and n < 100 then -- two digits, each squeezed into half the tile
					add("text_" .. n // 10, true, 0, x, y, m[1], m[2], u.values[ZLAYER], 1, nil, nil, ms)
					add("text_" .. n % 10, true, 0, x, y, m[1], m[2], u.values[ZLAYER], 2, nil, nil, ms)
				elseif u.values[VISUALSTYLE] == 1 and n < 26 then
					add("text_" .. string.char(97 + n), true, 0, x, y, m[1], m[2], u.values[ZLAYER], nil, nil, nil, ms)
				elseif u.values[VISUALSTYLE] == 2 then
					add("dot", true, 0, x, y, m[1], m[2], u.values[ZLAYER], nil, nil, nil, ms)
				end
			end
		elseif t and u.visible then
			local tiling = u.values[TILING]
			local frame = 0
			-- tiling 1 (dynamictiling) and 2/3/4 (animate) keep their sprite frame in unit.direction
			if tiling == 1 or tiling >= 2 then frame = u.direction
			elseif tiling == 0 then frame = u.values[DIR] * 8 end
			local c = u.colour or t.colour or TILE_COLOUR
			local ch = changes[u.className] or NONE
			local root = t.sprite_in_root ~= false
			if ch.root ~= nil then root = ch.root end
			local x, y, p = u.values[XPOS], u.values[YPOS], engine.prev[u.fixed]
			local fx, fy = x, y
			if p and math.abs(x - p[1]) <= 1 and math.abs(y - p[2]) <= 1 then fx, fy = p[1], p[2] end -- slide one-tile moves; jumps snap
			add(ch.image or t.sprite or u.className, root, frame, x, y, c[1], c[2], u.values[ZLAYER], nil, fx, fy)
		end
	end
	for id in pairs(engine.blocked) do
		local o = engine.objects[id]
		local c = o and o.colour or CROSS_COLOUR
		if o and o.visible then add("#cross", true, 0, o.values[XPOS], o.values[YPOS], c[1], c[2], 20) end
	end
	if engine.ismap() then -- counters in the top-right corner: levels cleared, then orbs once there are any
		local w, row = generaldata.strings[WORLD], 1
		for _, c in ipairs({ { "flower", savenum(w .. "_prize", "total") }, { "orb", savenum(w .. "_bonus", "total") } }) do
			if c[2] > 0 or c[1] == "flower" then
				local digits = tostring(c[2])
				local x = roomsizex - 2 - #digits -- icon, then one tile per digit, ending at the right edge
				for i = 0, #digits do add("#box", true, 0, x + i, row, 0, 4, 30) end
				add(c[1], true, 0, x, row, 2, 4, 31)
				for i = 1, #digits do add("text_" .. digits:sub(i, i), true, 0, x + i, row, 0, 3, 31) end
				row = row + 1
			end
		end
	end

	-- draw order: by layer, then as added (a stable sort; there are only a few layers)
	local n, layer, order = dl.n, dl.layer, dl.order
	local seen, layers = {}, {}
	for i = 1, n do
		local l = layer[i]
		if not seen[l] then seen[l], layers[#layers + 1] = true, l end
	end
	table.sort(layers)
	local j = 0
	for _, l in ipairs(layers) do
		for i = 1, n do
			if layer[i] == l then j = j + 1; order[j] = i end
		end
	end
	return dl
end

-- The draw list as one table per item ({ sprite, root, frame, x, y, c1, c2, layer, part, fx, fy, shade }), in draw
-- order: for tests.
function engine.drawitems()
	local d = engine.drawlist()
	local items = {}
	for j = 1, d.n do
		local i, e = d.order[j], {}
		for k, f in ipairs(DL_FIELDS) do e[k] = d[f][i] end
		items[j] = e
	end
	return items
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
