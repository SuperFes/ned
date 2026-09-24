module Sample exposing (classify, total)


type Shape
    = Circle Float
    | Rect Float Float


classify : Int -> String
classify n =
    case compare n 0 of
        LT ->
            "negative"

        EQ ->
            "zero"

        GT ->
            if n > 100 then
                "large"

            else
                "small"


total : List Int -> Int
total xs =
    let
        step x acc =
            acc + x
    in
    List.foldl step 0 xs
