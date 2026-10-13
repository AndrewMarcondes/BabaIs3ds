-- Save slots: writes alternate between .a and .b, loading takes the newest complete one.
local path = engine.savefile
MF_store("save", "savetest", "k", 1) engine.flushsave()
local g1 = engine.savegen
MF_store("save", "savetest", "k", 2) engine.flushsave()
assert(engine.savegen == g1 + 1, "each changed save should bump the generation")
assert(engine.loadsave().savetest.k == "2", "load should return the newest save")
local before = engine.savegen
engine.flushsave()
MF_store("save", "savetest", "k", 2) engine.flushsave()
assert(engine.savegen == before, "an unchanged save should not be written")

-- a torn write: the newest slot loses its #end -> the previous one is used
local newest = path .. (engine.savegen % 2 == 1 and ".a" or ".b")
local f = io.open(newest, "r+b") f:seek("set", 0) f:write("#gen=999\n[savetest]\nk=garbage\n") f:close()
local f2 = io.open(newest, "rb") local t = f2:read("a") f2:close()
local f3 = io.open(newest, "wb") f3:write((t:gsub("#end", "#xxx"))) f3:close()
assert(engine.loadsave().savetest.k == "1", "a slot without #end must be ignored")

-- slots keep their size (rewritten in place, no new allocation)
local function size(p) local h = io.open(p, "rb") local n = h:seek("end") h:close() return n end
local sa, sb = size(path .. ".a"), size(path .. ".b")
for i = 3, 6 do MF_store("save", "savetest", "k", i) engine.flushsave() end
assert(size(path .. ".a") == sa and size(path .. ".b") == sb, "slots should be rewritten in place")
print("save slots ok")
