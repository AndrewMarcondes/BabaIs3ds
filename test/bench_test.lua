-- Pause menu > Benchmark (engine.benchmarks in the shim): every scenario loads, plays its moves without winning, and
-- undo brings back its start; afterwards the player is back in their level and the save is unchanged.
local function savestr() local o = {} for s, t in pairs(engine.ini.save) do for k, v in pairs(t) do o[#o+1] = s .. "." .. k .. "=" .. tostring(v) end end table.sort(o) return table.concat(o, ";") end
assert(engine.gotolevel("1level"))
engine.input("right")
local before, file = savestr(), engine.savefile
engine.benchbegin()
local i = 1
while true do
	local name, moves = engine.benchsetup(i)
	if not name then break end
	for _ = 1, 2 do
		for _, k in ipairs(moves) do engine.input(k) end
		assert(not engine.won, name .. " won")
		for _ = 1, #moves do engine.input("undo") end
	end
	print(("scenario %d: %s, %d moves ok"):format(i, name, #moves))
	i = i + 1
end
engine.fullgc()
assert(engine.benchend())
assert(engine.shown == "1level", "not back in 1level: " .. tostring(engine.shown))
assert(savestr() == before, "save changed")
assert(engine.savefile == file, "savefile not restored")
print("back in " .. engine.shown .. ", save unchanged")
print("PASS")
