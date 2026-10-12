-- Engine side of the Lua boundary (what Chowdren/Fusion provided). Game objects are plain tables.

engine = { objects = {}, missing = {} }

local function newobj(id)
	return { fixed = id, values = {}, strings = {}, flags = {}, className = "", direction = 0 }
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

-- Tile map layers: map[0] etc. Real methods filled in as the scripts reveal them.
TileMap = {}
function TileMap.new(id)
	return { id = id }
end

-- Any MF_* not defined yet becomes a stub that records its first call.
setmetatable(_G, { __index = function(t, k)
	if type(k) == "string" and k:sub(1, 3) == "MF_" then
		local f = function(...)
			engine.missing[k] = (engine.missing[k] or 0) + 1
		end
		rawset(t, k, f)
		return f
	end
end })
