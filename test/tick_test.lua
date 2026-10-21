-- engine.tick() says whether the picture may have changed, and C only rebuilds its draw list when it does:
-- on every level of the baba world, a tick that reports no change must leave the draw list exactly as it was.
local dir = engine.data .. "/Worlds/" .. engine.world
local p = io.popen('ls "' .. dir .. '"')
local names = {}
for f in p:lines() do
	local n = f:match("^(.+)%.l$")
	if n then names[#names + 1] = n end
end
p:close()
table.sort(names)

local function snapshot()
	local out = {}
	for i, e in ipairs(engine.drawitems()) do
		local f = {}
		for k = 1, 12 do f[k] = tostring(e[k]) end
		out[i] = table.concat(f, ",")
	end
	return table.concat(out, ";")
end

local fails, ticks, changed = 0, 0, 0
for _, n in ipairs(names) do
	assert(engine.gotolevel(n), "gotolevel failed: " .. n)
	for _, k in ipairs({ "", "right", "up", "", "left", "down" }) do
		if k ~= "" then engine.input(k) end
		for _ = 1, 4 do
			local before = snapshot()
			local c = engine.tick()
			ticks = ticks + 1
			if c then changed = changed + 1
			elseif snapshot() ~= before then
				fails = fails + 1
				print("FAIL " .. n .. ": a tick reported no change but the draw list changed")
				break
			end
			if engine.won then break end
		end
		if engine.won then break end
	end
end
print(("%d levels, %d ticks, %d needed a new draw list, %d wrong"):format(#names, ticks, changed, fails))
assert(fails == 0, "tick change detection missed changes")
print("PASS")
