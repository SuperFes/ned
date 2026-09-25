extends Node

var count := 0


func classify(n: int) -> String:
	if n < 0:
		return "negative"
	elif n == 0:
		return "zero"
	else:
		if n > 100:
			return "large"
		return "small"


func total(xs: Array) -> int:
	var acc := 0
	for x in xs:
		acc += x
	while acc > 1000:
		acc -= 1000
	return acc


class Inner:
	var value := 1

	func bump() -> void:
		value += 1


func describe(v) -> String:
	match v:
		1:
			return "one"
		[var a, ..]:
			return str(a)
		_:
			return "other"
