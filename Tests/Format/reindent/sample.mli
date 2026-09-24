module type S = sig
  type t
  val create :
    int -> t
  val name : t -> string
end

val top : int
