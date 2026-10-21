-- The faster hasfeature (engine.speedups in the shim) must not change the game: every level of the baba world is
-- played with the game's own hasfeature and with the fast one, from the same start and random seed, and the game
-- state (every unit, the rules) is compared after every turn. Also level 5 with WALL IS YOU (50 YOU units).
local dir = engine.data .. "/Worlds/" .. engine.world
local p = io.popen('ls "' .. dir .. '"')
local names = {}
for f in p:lines() do
	local n = f:match("^(.+)%.l$")
	if n then names[#names + 1] = n end
end
p:close()
table.sort(names)

local moves = { "right", "right", "up", "up", "left", "left", "left", "down", "down", "idle", "down", "right",
	"undo", "right", "up", "left", "idle", "down", "right", "right", "up", "undo", "undo", "left" }

local function state()
	local out = { tostring(engine.won), engine.message, #units }
	for _, u in ipairs(units) do
		out[#out + 1] = table.concat({ u.strings[UNITNAME], u.values[XPOS], u.values[YPOS], u.values[DIR], u.direction,
			u.values[ID], tostring(u.flags[DEAD]) }, ",")
	end
	for _, f in ipairs(features) do
		out[#out + 1] = table.concat(f[1], " ") .. "/" .. #f[2]
	end
	return table.concat(out, ";")
end

local function deepcopy(t)
	local c = {}
	for k, v in pairs(t) do c[k] = type(v) == "table" and deepcopy(v) or v end
	return c
end

local save = deepcopy(engine.ini.save)

-- Plays the moves with the given hasfeature; returns the state after each turn (or nil if the level doesn't load).
local function play(level, impl, setup)
	hasfeature = impl
	engine.ini.save = deepcopy(save)
	math.randomseed(1)
	if not engine.gotolevel(level) then return nil end
	if setup then setup() end
	local states = { state() }
	for _, k in ipairs(moves) do
		if k == "idle" then k = engine.ismap() and "undo" or "enter" end -- "enter" on a map would leave this level
		engine.input(k)
		states[#states + 1] = state()
	end
	return states
end

local function wallisyou() -- swap the WALL and BABA text: WALL IS YOU, BABA IS STOP
	local tw, tb
	for _, u in ipairs(units) do
		if u.strings[UNITNAME] == "text_wall" then tw = u end
		if u.strings[UNITNAME] == "text_baba" then tb = u end
	end
	local wx, wy, bx, by = tw.values[XPOS], tw.values[YPOS], tb.values[XPOS], tb.values[YPOS]
	update(tw.fixed, bx, by, tw.values[DIR])
	update(tb.fixed, wx, wy, tb.values[DIR])
	updatecode = 1
	code()
end

local cases = {}
for _, n in ipairs(names) do cases[#cases + 1] = { n } end
cases[#cases + 1] = { "5level", wallisyou, "5level (wall is you)" }

local fails, turns, tgame, tfast = 0, 0, 0, 0
for _, c in ipairs(cases) do
	local t = os.clock()
	local a = play(c[1], engine.game.hasfeature, c[2])
	tgame = tgame + os.clock() - t
	t = os.clock()
	local b = play(c[1], engine.fast.hasfeature, c[2])
	tfast = tfast + os.clock() - t
	if a then
		turns = turns + #moves
		for i = 1, #a do
			if a[i] ~= b[i] then
				fails = fails + 1
				print(("FAIL %s: game state differs after turn %d (%s)"):format(c[3] or c[1], i - 1, i > 1 and moves[i - 1] or "start"))
				break
			end
		end
	end
end
hasfeature = engine.fast.hasfeature
engine.ini.save = save
print(("%d levels, %d turns compared, %d differ; game's hasfeature %.1fs, fast %.1fs"):format(#cases, turns, fails, tgame, tfast))
assert(fails == 0, "the fast hasfeature changed the game")
print("PASS")
