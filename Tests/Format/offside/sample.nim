import std/strutils

type
  Shape = object
    width: float
    height: float

proc classify(n: int): string =
  if n < 0:
    result = "negative"
  elif n == 0:
    result = "zero"
  else:
    if n > 100:
      result = "large"
    else:
      result = "small"

proc total(xs: seq[int]): int =
  for x in xs:
    result += x
  while result > 1000:
    result -= 1000

when isMainModule:
  echo classify(total(@[1, 2, 3]))
