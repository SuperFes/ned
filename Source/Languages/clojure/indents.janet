# smart-indentation follow-up. See c-indents.scm's own header comment for the
# general convention and for what the delimiter imprint contributes without a
# capture. Shared by ClojureMode and JankMode (same grammar),
# mirroring kClojure's own sharing. Checked against tree-sitter-clojure's own
# node-types.json.
#
# real-per-form-lisp-indent follow-up: see janet-indents.scm's own comment
# for the overall "aligned by default, indent.body for a short special-form
# table" design -- this mirrors it exactly, using the `name: (sym_name)`
# field access clojure.scm's own highlighting query already established for
# reading a symbol's bare name (Clojure's sym_lit carries an optional
# namespace/name split, unlike Janet's flat sym_lit). A call's arguments
# align under its first argument (@aligned.args), a collection's elements
# under its first element (@aligned).
(list_lit
  .
  (sym_lit
    name: (sym_name) @_head)
  (:any-of? @_head
    "let" "let*" "fn" "fn*" "do" "when" "when-not" "when-let" "when-first" "if"
    "if-not" "if-let" "if-some" "loop" "loop*" "for" "doseq" "dotimes" "while"
    "defn" "defn-" "def" "defmacro" "defmacro-" "defrecord" "deftype" "defprotocol"
    "with-open" "with-local-vars" "binding" "cond" "cond->" "cond->>" "case"
    "try" "catch" "finally" "ns" "->" "->>" "as->" "comment")) @indent.body

[(list_lit) (anon_fn_lit)] @aligned.args

# Collections line up under their first element (`(let [x 1` / `y 2]`).
[(vec_lit) (map_lit) (set_lit)] @aligned

