(ns demo.core)

;; The binding-vector case locals.scm unrolls by pair index.
(defn total [values]
  (let [sum 0
        doubled (map #(* 2 %) values)]
    (reduce + sum doubled)))

(def limit 10)

(defn shout [name]
  (clojure.string/upper-case name))
