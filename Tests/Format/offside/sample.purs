module Sample where

import Prelude

data Shape
  = Circle Number
  | Rect Number Number

classify :: Int -> String
classify n =
  case compare n 0 of
    LT -> "negative"
    EQ -> "zero"
    GT ->
      if n > 100 then "large"
      else "small"

total :: Array Int -> Int
total xs = go 0
  where
  go acc =
    let
      next = acc + 1
    in
      next
