-- Same allocation shape as testbench.lua, but the per-iteration
-- bookkeeping keeps a running numeric total instead of a reference to
-- the table itself, so 'obj' never escapes the iteration it's created
-- in. Used to show the escape analyzer's promotion actually firing
-- (Part D.3), since it provably cannot on the real testbench.lua (see
-- PART_C1_DESIGN.md and PART_A/D reports).
function test()
    local total = 0

    local m = 1000
    local n = 1000
    local survive_every = 100

    local count = 0
    for i = 1, m do
        for j = 1, n do
            count = count + 1

            local obj = {}
            obj[1] = "payload"
            obj[2] = {i, j}

            if count % survive_every == 0 then
                total = total + obj[2][1] + obj[2][2]
            end
        end
    end

    return total
end

test()
