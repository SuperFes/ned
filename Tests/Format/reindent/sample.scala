package demo

trait Shape {
  def area: Double
}

case class Circle(r: Double) extends Shape {
  def area: Double =
    math.Pi * r * r
}

object Main {
  val names = List(
    "a",
    "b"
  )

  def describe(s: Shape): String = s match {
    case Circle(r) if r > 1 =>
      "big"
    case _ => "other"
  }

  def main(args: Array[String]): Unit = {
    for (n <- names) {
      println(n)
    }
    val total =
      names.map(_.length).sum
    println(total)
  }
}
