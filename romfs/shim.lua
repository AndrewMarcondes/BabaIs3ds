-- Engine side of the Lua boundary (what Chowdren/Fusion provided). Game objects are plain tables.

engine = { objects = {}, missing = {}, nextid = 100 }

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

function MF_emptycreate(class, x, y)
	local id = engine.nextid
	engine.nextid = id + 1
	local o = mmf.newObject(id)
	o.className = class
	o.values[XPOS], o.values[YPOS] = x, y
	o.values[ID] = -1
	return id
end

function MF_create(class)
	return MF_emptycreate(class, 0, 0)
end

-- shortcut: effects/particles are created but never drawn; render them when visuals matter
MF_effectcreate = MF_create
MF_specialcreate = MF_create

function MF_remove(id) engine.objects[id] = nil end
MF_cleanremove = MF_remove

-- "a,b,c" -> {"a","b","c"}; empty string -> {}
function MF_parsestring(s)
	local t = {}
	for v in string.gmatch(s or "", "([^,]+)") do t[#t + 1] = v end
	return t
end

function MF_setcolour(id, c1, c2)
	local o = engine.objects[id]
	if o then o.colour = { c1, c2 } end
end

-- level: { w, h, layers = { { tiles = {...}, dirs = {...} }, ... } }, tiles 0-based row-major, ids = tile[1] + tile[2]*256
function engine.loadlevel(level)
	engine.current = level
	init(1, level.w, level.h, 24, 0, 0, 2, 3, 4, 5, 6, 7, 8, 400, 240)
	generaldata2.values[ZOOM] = 1
	spritedata.values[TILEMULT] = 1
	generaldata.values[DISABLEPARTICLES] = 1
	changes = changes or {} -- shortcut: .ld object overrides not loaded yet; they belong here
	clearunits()
	-- shortcut: old level's unit objects are never freed; drop them here when level switching exists
	engine.edge = {}

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
	dynamictiling()
	updatecolours()
	startblock()
end

-- shortcut: restart is a plain reload, so it can't be undone like in the real game
function MF_restart(instant)
	engine.loadlevel(engine.current)
end

-- One player input per turn: key is a name from the game's `keys` table (right/up/left/down/idle/undo/restart).
function engine.input(key)
	if key == "undo" then
		undo()
		updatecode = 1
		code()
	else
		command(key)
		block()
		newundo()
	end
end

-- Units in draw order: { sprite, root, frame, x, y, c1, c2 }
function engine.drawlist()
	local list = {}
	for _, u in ipairs(units) do
		local t = tileslist[u.className]
		if t and u.visible then
			local tiling = u.values[TILING]
			local frame = 0
			if tiling == 1 then frame = u.direction
			elseif tiling == 0 or tiling == 2 or tiling == 3 then frame = u.values[DIR] * 8 end
			local c = u.colour or t.colour or { 0, 3 }
			list[#list + 1] = { t.sprite or u.className, t.sprite_in_root ~= false, frame,
				u.values[XPOS], u.values[YPOS], c[1], c[2], u.values[ZLAYER] }
		end
	end
	table.sort(list, function(a, b) return a[8] < b[8] end)
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
