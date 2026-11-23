-- 'promoted' must be a candidate: never returned, captured, stored, or
-- passed to an unwhitelisted call. Its child string is intentionally
-- long enough to not be an interned short string, so it's a real
-- separately-allocated GCObject the arena table has to keep alive.
local function promoted_survives_gc()
  local t = {}
  t[1] = string.rep("payload-", 50)
  t[2] = {99, 100}
  collectgarbage("collect")   -- forced full GC while 't' is still live
  local len = #t[1]
  local nested = t[2][1] + t[2][2]
  collectgarbage("collect")   -- and again, after touching its contents
  return len, nested
end

local a, b = promoted_survives_gc()
assert(a == 400, "string child did not survive: got " .. tostring(a))
assert(b == 199, "nested table child did not survive: got " .. tostring(b))

-- many frames in a row, to exercise arena reuse/chaining across calls
local function churn()
  local t = {}
  for i = 1, 50 do
    t[i] = {i, i * 2}
  end
  local sum = 0
  for i = 1, 50 do sum = sum + t[i][1] + t[i][2] end
  return sum
end
for i = 1, 2000 do
  local s = churn()
  assert(s == 3825, "churn sum mismatch: " .. tostring(s))
end

-- a candidate that DOES escape (via return) must still behave correctly
-- on the normal heap path -- this exercises the "bail to heap" branch
local function returns_table()
  local t = {}
  t.x = 42
  return t
end
local rt = returns_table()
assert(rt.x == 42)
collectgarbage("collect")
assert(rt.x == 42, "heap-path table corrupted after GC")

print("arenatest: all assertions passed")
