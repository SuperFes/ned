type point = {
  x : float;
  y : float;
}

module type SHAPE = sig
  val area : float -> float
  val name : string
end

let rec even n =
  if n = 0 then true
  else odd (n - 1)

and odd n =
  if n = 0 then false
  else even (n - 1)

let describe = function
  A -> "a"
  | B -> "b"

let safe_div a b =
  try
    Some (a / b)
  with
  | Division_by_zero -> None

let print_all xs =
  List.iter
    (fun x ->
      print_int x;
      print_newline ())
    xs

let sum_pairs =
  [
    (1, 2);
    (3, 4);
  ]

let run () =
  begin
    print_string "start";
    print_newline ()
  end;
  let p = { x = 1.0; y = 2.0 } in
  match p.x with
  | 0.0 -> ()
  | _ ->
    match p.y with
    | 0.0 -> ()
    | _ -> print_string "both"
