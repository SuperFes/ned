object Three:
  def sign(n: Int): String =
    if n > 0 then
      "pos"
    else
      "neg"

  def kind(n: Int): String = n match
    case 0 => "zero"
    case _ =>
      "other"

  def safe(n: Int): Int =
    try
      10 / n
    catch
      case _: ArithmeticException => 0
    finally
      println("done")

  val doubled = List(1, 2).map { x =>
    x * 2
  }
