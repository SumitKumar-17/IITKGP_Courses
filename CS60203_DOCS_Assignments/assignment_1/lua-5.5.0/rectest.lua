local function rec(n)
  local t = {}
  t[1] = n
  if n > 0 then
    t[2] = rec(n - 1)
  else
    t[2] = 0
  end
  collectgarbage()
  return t[1] + t[2]
end
local r = rec(200)
assert(r == 20100, "recursion arena test failed: " .. tostring(r))
print("rectest OK")

-- pcall/error unwind test: arena frames must be freed correctly on error
local function boom(n)
  local t = {}
  t[1] = {n, n}
  if n <= 0 then error("boom") end
  return boom(n - 1)
end
for i = 1, 500 do
  local ok, err = pcall(boom, 20)
  assert(not ok)
end
collectgarbage()
print("pcall unwind test OK")
