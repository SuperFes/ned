(fn area [shape]
  (let [w shape.w
        h shape.h]
    (if (> w 0)
        (* w h)
        0)))
(local t {:a 1
          :b 2})
(each [_ x (ipairs xs)]
  (print x))
(when ok
  (print :yes))
(foo bar
     baz)
