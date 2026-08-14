-- Same shape as promobench.lua, but each promotable table lives in its
-- own short-lived call instead of accumulating inside one long-running
-- frame: the intended use case for frame-arena promotion (a temporary
-- table that is created, used, and discarded within one call).
local function makeobj(i, j)
    local obj = {}
    obj[1] = "payload"
    obj[2] = {i, j}
    return obj[2][1] + obj[2][2]
end

function test()
    local total = 0

    local m = 1000
    local n = 1000
    local survive_every = 100

    local count = 0
    for i = 1, m do
        for j = 1, n do
            count = count + 1
            local v = makeobj(i, j)
            if count % survive_every == 0 then
                total = total + v
            end
        end
    end

    return total
end

test()
