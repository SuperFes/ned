type shape =
  | Circle of float
  | Rect of float * float

let classify n =
  match compare n 0 with
  | -1 -> "negative"
  | 0 -> "zero"
  | _ ->
    if n > 100 then
      "large"
    else
      "small"

let total xs =
  let step acc x =
    acc + x
  in
  List.fold_left step 0 xs

module Inner = struct
  let area = function
    | Circle r -> 3.14 *. r *. r
    | Rect (w, h) -> w *. h
end
