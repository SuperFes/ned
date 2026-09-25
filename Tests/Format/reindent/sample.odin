package main

import "core:fmt"

Point :: struct {
	x: int,
	y: int,
}

main :: proc() {
	for i in 0 ..< 3 {
		if i > 1 {
			fmt.println(i)
		} else {
			fmt.println("small")
		}
	}
}
