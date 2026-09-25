type shape =
  | Circle(float)
  | Square(float)

type point = {
  x: float,
  y: float,
}

let area = s => {
  switch s {
  | Circle(r) => r *. r
  | Square(x) => x *. x
  }
}

let add = (
  a,
  b,
) => a + b

let total =
  add(1, 2) + add(3, 4) + add(5, 6) + add(7, 8) + add(9, 10) + add(11, 12)

let describe = s =>
  switch s {
  | Circle(_) =>
    "a circle with a rather long description that wraps onto its own line"
  | Square(_) => "square"
  }

let safe = () =>
  try {
    risky()
  } catch {
  | Not_found => 0
  | _ => 1
  }
