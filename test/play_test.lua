-- Run by build-host/script_test after engine.boot() with an empty save.
local function find(name)
	for _, u in ipairs(units) do if u.strings[UNITNAME] == name then return u end end
end
local function rules()
	local t = {}
	for _, f in ipairs(features) do t[#t + 1] = table.concat(f[1], " ") end
	return table.concat(t, ", ")
end

-- level 1
assert(generaldata.strings[CURRLEVEL] == "0level", "fresh save should start on level 1, got " .. generaldata.strings[CURRLEVEL])
assert(engine.music == "baba", "level 1 should play the main theme, got " .. tostring(engine.music))
local b = find("baba")
local x0 = b.values[XPOS]
print("level 1:", #units, "units; rules: " .. rules())
local f0 = b.direction
for i = 1, 3 do engine.input("right") end
assert(b.values[XPOS] == x0 + 3, "baba did not move")
local played = engine.takesounds()
assert(#played >= 1 and played[#played]:match("^move"), "a move should queue a movement sound, got " .. table.concat(played, ","))
assert(b.direction ~= f0 and b.direction // 8 == 0, "walking right should advance the walk frame, got " .. b.direction)
engine.input("up")
assert(b.direction // 8 == 1, "facing up should use the up frames, got " .. b.direction)
engine.input("undo")
engine.takesounds()
engine.input("undo")
assert(b.values[XPOS] == x0 + 2, "undo did not step back")
assert((engine.takesounds()[1] or ""):match("^undo%d$"), "undo should queue an undo sound")
local function rockat(x, y)
	for _, u in ipairs(units) do
		if u.strings[UNITNAME] == "rock" and u.values[XPOS] == x and u.values[YPOS] == y then return u end
	end
end
local r = rockat(x0 + 4, b.values[YPOS])
assert(r, "no rock in front of baba")
engine.input("right"); engine.input("right")
assert(b.values[XPOS] == x0 + 4 and r.values[XPOS] == x0 + 5, "rock not pushed")
engine.input("restart")
b = find("baba")
assert(b.values[XPOS] == x0, "restart did not reset baba")
print("move/undo/push/restart ok")

-- win -> map
for i = 1, 30 do
	if engine.won then break end
	engine.input("right")
end
assert(engine.won and generaldata.strings[CURRLEVEL] == "0level", "touching the flag should win and stay on the level for a moment")
engine.input("left")
for i = 1, 5 do engine.tick() end -- the win pause
assert(generaldata.strings[CURRLEVEL] == "106level", "win should return to the map, at " .. generaldata.strings[CURRLEVEL])
assert(engine.ismap(), "106level should be a map")
assert(engine.music == "map", "the map should play the map theme, got " .. tostring(engine.music))
assert(MF_read("save", "baba", "0level") == "3", "win not saved")
assert(MF_read("save", "baba_prize", "total") == "1", "a first win should count one level clear")
print("on map; message: " .. engine.message:gsub("\n", " / "))

local cursor = find("cursor")
assert(cursor, "no cursor on map")
local function at(u) return u.values[XPOS] .. "," .. u.values[YPOS] end
local levels = {}
for _, u in ipairs(units) do
	if u.className == "level" then levels[u.strings[U_LEVELFILE]] = u end
end
assert(at(cursor) == "10,16", "cursor should start on the level 1 icon, at " .. at(cursor))
assert(levels["1level"].values[COMPLETED] == 2, "winning level 1 should open 'where do i go?'")
local open = 0
for _, p in ipairs(paths) do if engine.objects[p].values[COMPLETED] >= 1 then open = open + 1 end end
assert(open >= 2, "paths to the next level should be revealed")

-- walk the revealed path and enter the next level
for _, k in ipairs({ "right", "up", "up" }) do engine.input(k) end
assert(at(cursor) == "11,14", "cursor should follow the path to level 2, at " .. at(cursor))
engine.input("left")
assert(at(cursor) == "11,14", "cursor should not leave the path")
engine.input("enter")
assert(generaldata.strings[CURRLEVEL] == "1level", "enter should load 1level, at " .. generaldata.strings[CURRLEVEL])
assert(find("baba"), "level 2 has no baba")
print("entered: " .. engine.message)

-- leave back to the map
engine.input("leave")
assert(generaldata.strings[CURRLEVEL] == "106level", "leave should return to the map")
cursor = find("cursor")
assert(at(cursor) == "11,14", "cursor should be back on level 2's icon, at " .. at(cursor))
print("map navigation ok")
for k, n in pairs(engine.missing) do io.write(k, "=", n, " ") end print()
