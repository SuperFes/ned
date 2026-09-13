-- A small module exercising functions, locals, control flow and tables.
local M = {}

local DEFAULT_LIMIT = 10

function M.greet(name)
    local message = "hello, " .. name
    print(message)
    return message
end

function M.count(items, limit)
    limit = limit or DEFAULT_LIMIT
    local total = 0
    for index, value in ipairs(items) do
        if index > limit then
            break
        end
        total = total + value
    end
    return total
end

while false do
    M.never()
end

return M
