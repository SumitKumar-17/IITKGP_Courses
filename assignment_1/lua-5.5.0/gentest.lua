collectgarbage("generational")
local function f()
  local t = {}
  t[1] = string.rep("x", 500)
  t[2] = {1,2,3}
  collectgarbage()
  return #t[1], t[2][1]+t[2][2]+t[2][3]
end
local a,b = f()
assert(a==500 and b==6, "gen mode arena-guard test failed: "..tostring(a).." "..tostring(b))
print("gentest OK")
