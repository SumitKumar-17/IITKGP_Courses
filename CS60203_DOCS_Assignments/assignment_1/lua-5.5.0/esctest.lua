-- one function per rule; names make the expected verdict obvious

local function baseline()          -- expect: promoted
  local t_baseline = {}
  return 1
end

local function ret_escape()        -- expect: escapes (rule 1, direct)
  local t_ret = {}
  return t_ret
end

local function ret_escape_multi()  -- expect: escapes (rule 1, non-trailing)
  local t_retmulti = {}
  return t_retmulti, 1
end

local function capture_escape()    -- expect: escapes (rule 2)
  local t_cap = {}
  local function inner() return t_cap end
  return inner
end

local function store_into_escaping()  -- expect: BOTH escape (rule 3 -> propagated)
  local a_container = {}
  local t_stored = {}
  a_container.x = t_stored
  return a_container
end

local function store_into_local_only()  -- expect: BOTH promoted (safe nesting)
  local a_local = {}
  local t_nested = {}
  a_local.x = t_nested
  return 1
end

local function store_into_global()  -- expect: escapes (rule 3, global container)
  local t_global = {}
  g_container = {}
  g_container.x = t_global
  return 1
end

local function unknown_call()      -- expect: escapes (rule 4)
  local t_call = {}
  local function sink(x) end
  sink(t_call)
  return 1
end

local function whitelisted_calls() -- expect: promoted (rule 4 whitelist)
  local t_wl = {}
  print(t_wl)
  local ty = type(t_wl)
  return 1
end

local function pairs_in_forloop()  -- expect: promoted (pairs whitelisted only here)
  local t_pairs = {}
  for k, v in pairs(t_pairs) do end
  return 1
end

local function pairs_outside_forloop()  -- expect: escapes (pairs NOT whitelisted here)
  local t_pairsbad = {}
  local it = pairs(t_pairsbad)
  return 1
end

local function alias_escape()      -- expect: escapes (aliased then returned)
  local t_alias = {}
  local a = t_alias
  return a
end

baseline()
ret_escape()
ret_escape_multi()
capture_escape()
store_into_escaping()
store_into_local_only()
store_into_global()
unknown_call()
whitelisted_calls()
pairs_in_forloop()
pairs_outside_forloop()
alias_escape()
