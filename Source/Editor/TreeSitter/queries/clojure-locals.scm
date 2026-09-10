;; Local-binding query -- see c-locals.scm's header for the three rules every
;; locals query here follows. Shared by ClojureMode and JankMode, which use
;; the same grammar (Editor/Mode.h).
;;
;; A Lisp grammar carries no semantic structure at all: `(let [x 1] ...)`,
;; `(defn f [a] ...)` and `(println x)` are the same node type, a list_lit of
;; children, and every binding form is a macro the grammar has never heard
;; of. So every pattern here dispatches on the HEAD SYMBOL's own text via
;; #any-of?, which is the only handle there is.
;;
;; The binding-vector patterns below are unrolled by pair index rather than
;; written as one "every sym_lit in the vector" pattern, and that is the
;; load-bearing detail in this file. A binding vector alternates name and
;; value (`[a 1 b 2]`), and a value is often itself a bare symbol
;; (`[alias name]`). The naive whole-vector pattern captures that value as a
;; DEFINITION -- so in `(let [a 1 b c] ...)`, the outer binding `c` would be
;; shadowed by a binding of itself, and renaming the real `c` would rewrite
;; only the occurrences outside this let. That is a corrupted rename, not a
;; miss, which is why the anchored form is worth the repetition: `. (_) . (_)`
;; per preceding pair pins each capture to an even index, so a value is never
;; mistaken for a name.
;;
;; The unrolling stops at eight pairs. A ninth binding in one vector is not
;; captured, so a use of it resolves outward and rename-symbol declines or
;; defers to a language server -- the same degradation every other
;; uncapturable construct here gets.
;;
;; Deliberately not captured, all of which degrade the same way: destructuring
;; (`[{:keys [x y]} m]`, `[[a b] pair]` -- the names are not direct children
;; of the binding vector), multi-arity `defn` bodies, and `binding`/`with-*`
;; forms whose head is not in the lists below.

;; Scopes. Every form that introduces names for its own body, captured whole
;; so the parameter/binding vector sits inside its own scope (rule 1).
((list_lit
   .
   (sym_lit name: (sym_name) @_scope_head)) @local.scope
 (#any-of? @_scope_head
   "defn" "defn-" "defmacro" "defmethod" "fn" "fn*"
   "let" "let*" "loop" "loop*" "when-let" "if-let" "when-some" "if-some"
   "doseq" "for" "with-open" "with-local-vars" "binding"))

;; `(def x 1)` and `(defonce x 1)`. Neither head is a scope above, so the
;; name they bind has no enclosing scope and reads as file-level -- which is
;; the point: capturing it is what lets LocalScopes.h tell "shadowed by an
;; outer binding" apart from "nothing in this file binds this name at all"
;; (c-locals.scm's header). A `defn`'s own name is deliberately NOT captured
;; here: it sits inside the form that is its own scope, so it would look like
;; a local of itself (rule 3).
((list_lit
   .
   (sym_lit name: (sym_name) @_def_head)
   .
   (sym_lit name: (sym_name) @local.definition.var))
 (#any-of? @_def_head "def" "def-" "defonce"))

;; An anonymous #(...) reader form binds %, %1, %2 -- a scope with no
;; definition capture, so those never resolve to a rename. Captured anyway so
;; a `let` nested inside one still finds its own enclosing chain correctly.
(anon_fn_lit) @local.scope

;; Parameters: every direct symbol of the argument vector, minus the `&`
;; rest-args marker, which is punctuation the grammar happens to spell as a
;; symbol rather than a name anything binds. `(defn f [a b])`
;; and `(defn f "doc" [a b])` are both spelled out; the vector is anchored
;; rather than matched anywhere in the form, since a `(defn f [a] [x y])`
;; body returning a literal vector would otherwise capture x and y as
;; parameters.
((list_lit
   .
   (sym_lit name: (sym_name) @_defn_head)
   .
   (sym_lit)
   .
   (vec_lit
     (sym_lit name: (sym_name) @local.definition.parameter)))
 (#any-of? @_defn_head "defn" "defn-" "defmacro" "defmethod")
 (#not-eq? @local.definition.parameter "&"))
((list_lit
   .
   (sym_lit name: (sym_name) @_defn_head)
   .
   (sym_lit)
   .
   (str_lit)
   .
   (vec_lit
     (sym_lit name: (sym_name) @local.definition.parameter)))
 (#any-of? @_defn_head "defn" "defn-" "defmacro" "defmethod")
 (#not-eq? @local.definition.parameter "&"))
((list_lit
   .
   (sym_lit name: (sym_name) @_fn_head)
   .
   (vec_lit
     (sym_lit name: (sym_name) @local.definition.parameter)))
 (#any-of? @_fn_head "fn" "fn*")
 (#not-eq? @local.definition.parameter "&"))
((list_lit
   .
   (sym_lit name: (sym_name) @_fn_head)
   .
   (sym_lit)
   .
   (vec_lit
     (sym_lit name: (sym_name) @local.definition.parameter)))
 (#any-of? @_fn_head "fn" "fn*")
 (#not-eq? @local.definition.parameter "&"))

;; Binding-vector names, one pattern per even index. See this file's header
;; for why this is unrolled rather than written as one pattern.
((list_lit
   .
   (sym_lit name: (sym_name) @_bind_head)
   .
   (vec_lit . (sym_lit name: (sym_name) @local.definition.var) . (_)))
 (#any-of? @_bind_head
   "let" "let*" "loop" "loop*" "when-let" "if-let" "when-some" "if-some"
   "doseq" "for" "with-open" "with-local-vars" "binding"))
((list_lit
   .
   (sym_lit name: (sym_name) @_bind_head)
   .
   (vec_lit . (_) . (_) . (sym_lit name: (sym_name) @local.definition.var) . (_)))
 (#any-of? @_bind_head
   "let" "let*" "loop" "loop*" "when-let" "if-let" "when-some" "if-some"
   "doseq" "for" "with-open" "with-local-vars" "binding"))
((list_lit
   .
   (sym_lit name: (sym_name) @_bind_head)
   .
   (vec_lit . (_) . (_) . (_) . (_) . (sym_lit name: (sym_name) @local.definition.var) . (_)))
 (#any-of? @_bind_head
   "let" "let*" "loop" "loop*" "when-let" "if-let" "when-some" "if-some"
   "doseq" "for" "with-open" "with-local-vars" "binding"))
((list_lit
   .
   (sym_lit name: (sym_name) @_bind_head)
   .
   (vec_lit . (_) . (_) . (_) . (_) . (_) . (_) . (sym_lit name: (sym_name) @local.definition.var) . (_)))
 (#any-of? @_bind_head
   "let" "let*" "loop" "loop*" "when-let" "if-let" "when-some" "if-some"
   "doseq" "for" "with-open" "with-local-vars" "binding"))
((list_lit
   .
   (sym_lit name: (sym_name) @_bind_head)
   .
   (vec_lit . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (sym_lit name: (sym_name) @local.definition.var) . (_)))
 (#any-of? @_bind_head
   "let" "let*" "loop" "loop*" "when-let" "if-let" "when-some" "if-some"
   "doseq" "for" "with-open" "with-local-vars" "binding"))
((list_lit
   .
   (sym_lit name: (sym_name) @_bind_head)
   .
   (vec_lit . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (sym_lit name: (sym_name) @local.definition.var) . (_)))
 (#any-of? @_bind_head
   "let" "let*" "loop" "loop*" "when-let" "if-let" "when-some" "if-some"
   "doseq" "for" "with-open" "with-local-vars" "binding"))
((list_lit
   .
   (sym_lit name: (sym_name) @_bind_head)
   .
   (vec_lit . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (sym_lit name: (sym_name) @local.definition.var) . (_)))
 (#any-of? @_bind_head
   "let" "let*" "loop" "loop*" "when-let" "if-let" "when-some" "if-some"
   "doseq" "for" "with-open" "with-local-vars" "binding"))
((list_lit
   .
   (sym_lit name: (sym_name) @_bind_head)
   .
   (vec_lit . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (_) . (sym_lit name: (sym_name) @local.definition.var) . (_)))
 (#any-of? @_bind_head
   "let" "let*" "loop" "loop*" "when-let" "if-let" "when-some" "if-some"
   "doseq" "for" "with-open" "with-local-vars" "binding"))

;; References. Only the name half of an UNQUALIFIED symbol: `!namespace`
;; excludes `clojure.string/upper-case`, whose sym_name is `upper-case` but
;; which cannot be a local, and taking `name:` rather than the whole sym_lit
;; is what keeps a rename rewriting the bare name only.
(sym_lit
  !namespace
  name: (sym_name) @local.reference)
