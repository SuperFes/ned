# .edn is data, not code, but it's read with Clojure's own reader syntax --
# same reasoning as .json. .bb is babashka, a Clojure dialect like jank but
# with no extra syntax of its own.

{:name "clojure"
 :extensions [".clj" ".cljs" ".cljc" ".edn" ".bb"]
 :line-comment ";"
 :lsp-root-markers ["deps.edn" "project.clj" "shadow-cljs.edn" "bb.edn" "build.boot"]
 :auto-pairs :lisp
 :import-resolution {:extensions ["clj" "cljc" "cljs"]
                     :module-substitutions [["-" "_"]]
                     :source-roots ["src" "test" "src/main/clojure" "src/test/clojure"]}
 :injection-aliases ["clj"]
}
