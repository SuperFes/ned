extends Node
class_name Foo
var x = 1


func f(a):
	if (a > 1):
		return 1
	elif a:
		return 2
	while (a):
		a -= 1


class Inner:
	func g():
		pass


	func h():
		pass
