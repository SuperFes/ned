;; Local-binding query -- see c-locals.scm's header for the three rules every
;; locals query here follows, and clojure-locals.scm's header for why the
;; binding-vector patterns are unrolled by pair index rather than written as
;; one whole-vector pattern. Everything said there about a Lisp grammar
;; carrying no semantic structure, and about dispatching on the head symbol's
;; own text, applies here unchanged.
;;
;; Two differences from the Clojure file, both grammar rather than language:
;; janet-simple's sym_lit is a leaf with no name/namespace fields (so a
;; reference is the whole sym_lit, and there is no qualified-symbol case to
;; exclude), and a bracketed tuple is a sqr_tup_lit rather than a vec_lit.
;;
;; `(def x ...)` and `(var x ...)` are Janet's dominant local-binding form --
;; far more so than `let` -- and they anchor cleanly as the second element of
;; the form, so they need none of the unrolling the binding vectors do. Their
;; enclosing scope is whichever captured form contains them; at top level
;; nothing does, which reads as file-level and is exactly right for a
;; module-level `def`.
;;
;; Deliberately not captured, all degrading to "declines" rather than to a
;; wrong rename: destructuring (`(def [a b] pair)`, `(let [{:x x} s] ...)`),
;; a ninth binding in one vector, and `(fn name [args] ...)`'s own name, which
;; sits inside the form that is its own scope (rule 3).

;; Scopes. Every form that introduces names for its own body, captured whole
;; so the parameter/binding tuple sits inside its own scope (rule 1).
((par_tup_lit
   .
   (sym_lit) @_scope_head) @local.scope
 (#any-of? @_scope_head
   "defn" "defn-" "defmacro" "defmacro-" "fn" "varfn"
   "let" "loop" "seq" "generate" "each" "eachp" "eachk" "with" "with-syms"
   "if-let" "when-let" "if-with" "when-with" "defer" "forv" "for"))

;; `(def x 1)`, `(var x 1)`, and their private/dynamic spellings. Anchored to
;; the second element, so a `(def x y)` value is never mistaken for a name.
((par_tup_lit
   .
   (sym_lit) @_def_head
   .
   (sym_lit) @local.definition.var)
 (#any-of? @_def_head "def" "def-" "var" "var-" "defglobal" "varglobal"))

;; Parameters: every direct symbol of the argument tuple, minus Janet's
;; `&`/`&opt`/`&keys`/`&named` arity markers, which the grammar spells as
;; symbols but which name nothing. `(defn f [a b])`,
;; `(defn f "doc" [a b])` and both `fn` spellings are written out; the tuple
;; is anchored rather than matched anywhere in the form, since a body
;; returning a literal tuple would otherwise capture its symbols as
;; parameters.
((par_tup_lit
   .
   (sym_lit) @_defn_head
   .
   (sym_lit)
   .
   (sqr_tup_lit
     (sym_lit) @local.definition.parameter))
 (#any-of? @_defn_head "defn" "defn-" "defmacro" "defmacro-" "varfn")
 (#not-any-of? @local.definition.parameter "&" "&opt" "&keys" "&named"))
((par_tup_lit
   .
   (sym_lit) @_defn_head
   .
   (sym_lit)
   .
   (str_lit)
   .
   (sqr_tup_lit
     (sym_lit) @local.definition.parameter))
 (#any-of? @_defn_head "defn" "defn-" "defmacro" "defmacro-" "varfn")
 (#not-any-of? @local.definition.parameter "&" "&opt" "&keys" "&named"))
((par_tup_lit
   .
   (sym_lit) @_fn_head
   .
   (sqr_tup_lit
     (sym_lit) @local.definition.parameter))
 (#eq? @_fn_head "fn")
 (#not-any-of? @local.definition.parameter "&" "&opt" "&keys" "&named"))
((par_tup_lit
   .
   (sym_lit) @_fn_head
   .
   (sym_lit)
   .
   (sqr_tup_lit
     (sym_lit) @local.definition.parameter))
 (#eq? @_fn_head "fn")
 (#not-any-of? @local.definition.parameter "&" "&opt" "&keys" "&named"))

;; `(each x xs body)` / `(eachp k v tbl body)` -- the loop variables are
;; positional, not bracketed, so they anchor directly.
((par_tup_lit
   .
   (sym_lit) @_each_head
   .
   (sym_lit) @local.definition.var)
 (#any-of? @_each_head "each" "eachk"))
((par_tup_lit
   .
   (sym_lit) @_each_head
   .
   (sym_lit) @local.definition.var
   .
   (sym_lit) @local.definition.var)
 (#any-of? @_each_head "eachp"))

;; Binding-tuple names, read pairwise in code -- see clojure-locals.scm's own
;; note on why the quantifier cannot live in the query and what stays here.
((par_tup_lit
   .
   (sym_lit) @_bind_head
   .
   (sqr_tup_lit) @local.definition.var.pairs)
 (#any-of? @_bind_head
   "let" "loop" "seq" "generate" "with" "with-syms" "if-let" "when-let"
   "forv" "for"))

;; References. A janet-simple sym_lit is a leaf, so the whole node is the
;; name -- there is no qualified-symbol shape to exclude the way Clojure's
;; sym_ns needs excluding.
(sym_lit) @local.reference
