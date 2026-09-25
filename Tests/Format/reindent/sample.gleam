import gleam/io

pub type Shape {
  Circle(radius: Float)
  Square(side: Float)
}

pub fn main() {
  let x = case 1 {
    1 -> {
      io.println("one")
      1
    }
    _ -> 0
  }
  x
}
