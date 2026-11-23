local function producer()
  local t = {}
  t[1] = 42
  coroutine.yield(t)
  return t[1]
end
local co = coroutine.create(producer)
local ok, val = coroutine.resume(co)
assert(ok and val[1] == 42, 'yield value wrong')
local ok2, val2 = coroutine.resume(co)
assert(ok2 and val2 == 42, 'resume value wrong')
print('corotest OK')
