#lang racket

(struct point (x y)
  #:transparent)

(define (area shape)
  (match shape
    [(point w h)
     (* w h)]
    [_ 0]))

(define (totals xs)
  (for/fold ([sum 0]
             [count 0])
            ([x (in-list xs)])
    (values (+ sum x)
            (add1 count))))

(module+ test
  (require rackunit)
  (check-equal? (area (point 2 3))
                6))
