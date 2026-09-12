(ns demo.cliff)

;; clojure-locals.scm unrolls binding pairs by index and stops at eight.
;; The ninth pair below is therefore NOT captured today -- see that file's
;; own header. This corpus entry exists to hold that limit in the snapshot,
;; so the trait engine lifting it shows up as a diff rather than as a claim.
(defn nine-bindings []
  (let [a 1
        b 2
        c 3
        d 4
        e 5
        f 6
        g 7
        h 8
        i 9]
    (+ a b c d e f g h i)))
