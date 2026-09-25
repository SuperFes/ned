module main

struct Point {
	x int
	y int
}

fn main() {
	for i in 0 .. 3 {
		if i > 1 {
			println(i)
		} else {
			println('small')
		}
	}
}
