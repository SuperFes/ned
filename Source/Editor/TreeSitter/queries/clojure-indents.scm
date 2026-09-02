; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. Shared by ClojureMode and JankMode (same grammar),
; mirroring kClojure's own sharing. Checked against tree-sitter-clojure's own
; node-types.json.
;
; real-per-form-lisp-indent follow-up: see janet-indents.scm's own comment
; for the overall "aligned by default, indent.body for a short special-form
; table" design -- this mirrors it exactly, using the `name: (sym_name)`
; field access clojure.scm's own highlighting query already established for
; reading a symbol's bare name (Clojure's sym_lit carries an optional
; namespace/name split, unlike Janet's flat sym_lit). vec_lit/map_lit/
; set_lit/anon_fn_lit aren't call forms and stay plain bracket-depth
; "@indent", unchanged.
(list_lit
  .
  (sym_lit
    name: (sym_name) @_head)
  (#any-of? @_head
    "let" "let*" "fn" "fn*" "do" "when" "when-not" "when-let" "when-first" "if"
    "if-not" "if-let" "if-some" "loop" "loop*" "for" "doseq" "dotimes" "while"
    "defn" "defn-" "def" "defmacro" "defmacro-" "defrecord" "deftype" "defprotocol"
    "with-open" "with-local-vars" "binding" "cond" "cond->" "cond->>" "case"
    "try" "catch" "finally" "->" "->>" "as->" "comment")) @indent.body

(list_lit) @aligned
(vec_lit) @indent
(map_lit) @indent
(set_lit) @indent
(anon_fn_lit) @indent

(list_lit ")" @dedent)
(vec_lit "]" @dedent)
(map_lit "}" @dedent)
(set_lit "}" @dedent)
(anon_fn_lit ")" @dedent)
