-- "changeless": FLAG IS ROCK turns the flag into a rock on the first move; the walkthrough then makes
-- ROCK IS FLAG (breaking ROCK IS ROCK and FLAG IS ROCK) and the rock must turn back into a flag.
local function count(n) local c = 0 for _, u in ipairs(units) do if u.strings[UNITNAME] == n then c = c + 1 end end return c end
local function textat(name, x, y)
	for _, u in ipairs(units) do
		if u.strings[UNITNAME] == "text_" .. name and u.values[XPOS] == x and u.values[YPOS] == y then return u end
	end
end
assert(engine.gotolevel("236level"))
engine.input("idle")
assert(count("rock") == 1 and count("flag") == 0, "FLAG IS ROCK should turn the flag into a rock")
update(textat("rock", 9, 2).fixed, 9, 3) -- push the second ROCK of ROCK IS ROCK aside
update(textat("flag", 7, 6).fixed, 9, 2) -- FLAG from FLAG IS ROCK -> ROCK IS FLAG
updatecode = 1 code()
engine.input("idle")
assert(count("flag") == 1 and count("rock") == 0, "a rock that was a flag must turn back into a flag")
print("re-transform ok")
