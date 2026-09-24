module Sample (classify, total) where

import Data.List (foldl')

data Shape
  = Circle Double
  | Rect Double Double
  deriving (Show)

classify :: Int -> String
classify n =
  case compare n 0 of
    LT -> "negative"
    EQ -> "zero"
    GT
      | n > 100 -> "large"
      | otherwise -> "small"

total :: [Int] -> Int
total xs = go 0 xs
  where
    go acc [] = acc
    go acc (y : ys) =
      let acc' = acc + y
       in go acc' ys

main :: IO ()
main = do
  let n = total [1, 2, 3]
  if n > 3
    then putStrLn (classify n)
    else pure ()
