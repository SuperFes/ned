package demo

type Point struct {
	X int
	Y int
}

type Shape interface {
	Area() float64
}

func Total(values []int) int {
	sum := 0
	for _, v := range values {
		sum += v
	}
	switch {
	case sum > 10:
		return 10
	default:
		return sum
	}
}
