# .edn is data, not code, but it's read with Clojure's own reader syntax --
# same reasoning as .json. .bb is babashka, a Clojure dialect like jank but
# with no extra syntax of its own.

{:name "clojure"
 :extensions [".clj" ".cljs" ".cljc" ".edn" ".bb"]
 :line-comment ";"
 :auto-pairs :lisp
 :import-resolution {:extensions ["clj" "cljc" "cljs"]}
 :injection-aliases ["clj"]
}
