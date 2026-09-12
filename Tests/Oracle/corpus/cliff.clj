(ns demo.cliff)

;; This file was added to hold a LIMIT: clojure-locals.scm unrolled binding
;; pairs by index and stopped at eight, so the ninth pair below was not
;; captured and a use of it was silently unrenameable. The snapshot recorded
;; exactly eight Definition.var captures and no `i`.
;;
;; The limit is closed -- the vector is now read pairwise in code (see that
;; file's own note) -- and the ninth capture appearing in the snapshot is what
;; the entry existed to show. It stays as the regression test it became: if
;; `i` ever stops being a definition again, this is where it shows up.
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
