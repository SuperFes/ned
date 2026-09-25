object A {
  val xs = for {
    x <- List(1, 2) ++
      List(3)
    y <- ys
  } yield x
  val s = a +
    b
  val t = items
    .map(f)
    .filter(g)
  def ok =
    a
    || b
    && c
}
