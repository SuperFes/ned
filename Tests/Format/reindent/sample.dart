import 'dart:math';

class Shape {
  final int sides;

  Shape(this.sides);

  String describe(int n) {
    switch (n) {
      case 0:
        return 'none';
      case 1:
      case 2:
        final label = 'few';
        return label;
      default:
        return 'many';
    }
  }

  int weight() => switch (sides) {
    3 => 1,
    4 => 2,
    _ => sides,
  };
}

void main() {
  final shapes = [
    Shape(3),
    Shape(4),
  ];
  for (final shape in shapes) {
    if (shape.sides > 3) {
      print(shape.describe(shape.sides));
    } else {
      shapes.forEach((s) {
        print(s.weight());
      });
    }
  }
}
