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

-- orbs: a won level with its bonus collected counts once, and opens orb gates (type 3)
generaldata.strings[CURRLEVEL] = "orbtest"
engine.won = false
MF_bonus(1)
MF_win()
assert(MF_read("save", "baba_bonus", "total") == "1", "collected bonus should count an orb")
engine.won, engine.bonus = false, false
MF_bonus(1)
MF_win()
assert(MF_read("save", "baba_bonus", "total") == "1", "the same level's bonus must not count twice")
engine.won = false
MF_bonus(1) MF_bonus(0) -- picked up then undone
generaldata.strings[CURRLEVEL] = "orbtest2"
MF_win()
assert(MF_read("save", "baba_bonus", "total") == "1", "an undone bonus must not count")
print("orbs ok")

-- endings record the flags the scripts read back
engine.won = false
generaldata.strings[CURRLEVEL] = "endtest"
MF_end()
assert(MF_read("save", "baba", "end") == "1" and engine.finished == "The End", "MF_end should save end=1")
print("endings ok")
