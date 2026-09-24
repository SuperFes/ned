module Shapes

export area

abstract type Shape end

struct Rect <: Shape
    w::Float64
    h::Float64
end

mutable struct Counter
    n::Int
end

function area(s::Rect)
    if s.w < 0
        throw(ArgumentError("negative"))
    elseif s.w == 0
        return 0.0
    else
        return s.w * s.h
    end
end

function total(shapes)
    sum = 0.0
    for s in shapes
        try
            sum += area(s)
        catch e
            @warn "skipped" e
        finally
            nothing
        end
    end
    weights = [
        1,
        2,
    ]
    map(shapes) do s
        let a = area(s)
            a * 2
        end
    end
    return sum
end

end
