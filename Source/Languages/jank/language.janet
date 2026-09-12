# jank is a Clojure dialect with no grammar of its own: Clojure's grammar
# and queries under a distinct name, so the mode line reads (jank-mode).

{:name "jank"
 :grammar "clojure"
 :extensions [".jank"]
 :line-comment ";"
 :auto-pairs :lisp
 :queries-from "clojure"
 :import-resolution {:extensions ["clj" "cljc" "cljs"]}
}
