def f
  x = 1 +
    2
  y = items
    .map { |i| i + 1 }
    .select do |i|
      i > 0
    end
  z = cond ?
    a :
    b
  return a &&
    b
end
