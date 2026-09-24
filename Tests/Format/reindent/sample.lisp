(defun area (shape)
  (let ((w (car shape))
        (h (cdr shape)))
    (if (> w 0)
        (* w h)
        0)))
(defmacro twice (x)
  `(progn ,x
     ,x))
(dolist (x '(1 2 3))
  (print x))
(loop for x in xs
      collect x)
