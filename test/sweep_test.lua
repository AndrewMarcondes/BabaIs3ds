-- Loads every level of the baba world through the shim and plays a few turns; reports Lua errors.
local dir = engine.data .. "/Worlds/baba"
local p = io.popen('ls "' .. dir .. '"')
local names = {}
for f in p:lines() do
	local n = f:match("^(.+)%.l$")
	if n then names[#names + 1] = n end
end
p:close()
table.sort(names)

local fails = 0
for _, n in ipairs(names) do
	local ok, err = xpcall(function()
		assert(engine.gotolevel(n), "gotolevel failed")
		for _, k in ipairs({ "right", "up", "left", "down", "undo", "enter" }) do engine.input(k) end
		engine.drawlist()
	end, debug.traceback)
	if not ok then
		fails = fails + 1
		print("FAIL " .. n .. " (" .. engine.message .. "): " .. tostring(err):gsub("\n.*", ""))
	end
end
print(("swept %d levels, %d failed"):format(#names, fails))
assert(fails == 0, "some levels failed")
