-- Map progress the shim reconstructs: gates opening on clear counts, maps counting as cleared (engine.mapprogress).
local function path(x, y)
	for _, id in ipairs(paths) do
		local p = engine.objects[id]
		if p.values[XPOS] == x and p.values[YPOS] == y then return p end
	end
end

-- gates on the main map: (12,5) needs 5 map clears, (16,4) needs 7; both revealed but locked
MF_store("save", "baba_clears", "total", 5)
MF_store("save", "baba", "106level,12,5", 1)
MF_store("save", "baba", "106level,16,4", 1)
assert(engine.gotolevel("106level"))
assert(path(12, 5).values[COMPLETED] == 3, "gate needing 5 map clears should open with 5")
assert(path(16, 4).values[COMPLETED] == 1, "gate needing 7 map clears should stay shut with 5")
assert(MF_read("save", "baba", "106level,12,5") == "3", "opened gate should be saved")
print("gates ok")

-- the lake counts as cleared once unlockcount (8) of its levels are
local lake = engine.readini(engine.data .. "/Worlds/baba/177level.ld")
local n = 0
for i = 0, tonumber(lake.general.levels) - 1 do
	local f = lake.levels[i .. "file"]
	if f ~= "106level" and n < 8 then
		MF_store("save", "baba", f, 3)
		n = n + 1
	end
end
sublevel("177level", 0, -1)
assert(engine.gotolevel("177level"))
assert(MF_read("save", "baba", "177level") == "3", "lake with 8 clears should count as cleared")
assert(MF_read("save", "baba_clears", "total") == "6", "map clear total should go up, got " .. MF_read("save", "baba_clears", "total"))
assert(engine.gotolevel("177level"))
assert(MF_read("save", "baba_clears", "total") == "6", "reloading a cleared map must not count it twice")
print("map clears ok")
