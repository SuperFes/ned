#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows. Shared by ClojureMode and JankMode, which use
#; the same grammar (Editor/Mode.h).
#;
#; A Lisp grammar carries no semantic structure at all: `(let [x 1] ...)`,
#; `(defn f [a] ...)` and `(println x)` are the same node type, a list_lit of
#; children, and every binding form is a macro the grammar has never heard
#; of. So every pattern here dispatches on the HEAD SYMBOL's own text via
#; #any-of?, which is the only handle there is.
#;
#; A binding vector alternates name and value (`[a 1 b 2]`), and a value is
#; often itself a bare symbol (`[alias name]`) -- so the naive whole-vector
#; pattern captures that value as a DEFINITION, and in `(let [a 1 b c] ...)`
#; the outer binding `c` ends up shadowed by a binding of itself. Renaming the
#; real `c` would then rewrite only the occurrences outside this let: a
#; corrupted rename, not a miss.
#;
#; Every-other-child is a quantifier, and a query pattern has none. This file
#; used to answer that by unrolling -- one pattern per even index, anchored
#; `. (_) . (_)` per preceding pair -- which worked, grew quadratically, and
#; stopped at eight pairs, after which a binding was silently unresolvable.
#; The vector is now captured whole as `@local.definition.var.pairs` and read
#; pairwise in code (Mode.cpp's ExpandPairwiseBindings). The language
#; knowledge -- which heads bind pairwise -- stays here; only the counting
#; moved.
#;
#; Deliberately not captured, all of which degrade the same way: destructuring
#; (`[{:keys [x y]} m]`, `[[a b] pair]` -- the names are not direct children
#; of the binding vector), multi-arity `defn` bodies, and `binding`/`with-*`
#; forms whose head is not in the lists below.

#; Scopes. Every form that introduces names for its own body, captured whole
#; so the parameter/binding vector sits inside its own scope (rule 1).
((list_lit
   .
   (sym_lit name: (sym_name) @_scope_head)) @local.scope
 (:any-of? @_scope_head
   "defn" "defn-" "defmacro" "defmethod" "fn" "fn*"
   "let" "let*" "loop" "loop*" "when-let" "if-let" "when-some" "if-some"
   "doseq" "for" "with-open" "with-local-vars" "binding"))

#; `(def x 1)` and `(defonce x 1)`. Neither head is a scope above, so the
#; name they bind has no enclosing scope and reads as file-level -- which is
#; the point: capturing it is what lets LocalScopes.h tell "shadowed by an
#; outer binding" apart from "nothing in this file binds this name at all"
#; (c-locals.scm's header). A `defn`'s own name is deliberately NOT captured
#; here: it sits inside the form that is its own scope, so it would look like
#; a local of itself (rule 3).
((list_lit
   .
   (sym_lit name: (sym_name) @_def_head)
   .
   (sym_lit name: (sym_name) @local.definition.var))
 (:any-of? @_def_head "def" "def-" "defonce"))

#; An anonymous #(...) reader form binds %, %1, %2 -- a scope with no
#; definition capture, so those never resolve to a rename. Captured anyway so
#; a `let` nested inside one still finds its own enclosing chain correctly.
(anon_fn_lit) @local.scope

#; Parameters: every direct symbol of the argument vector, minus the `&`
#; rest-args marker, which is punctuation the grammar happens to spell as a
#; symbol rather than a name anything binds. `(defn f [a b])`
#; and `(defn f "doc" [a b])` are both spelled out; the vector is anchored
#; rather than matched anywhere in the form, since a `(defn f [a] [x y])`
#; body returning a literal vector would otherwise capture x and y as
#; parameters.
((list_lit
   .
   (sym_lit name: (sym_name) @_defn_head)
   .
   (sym_lit)
   .
   (vec_lit
     (sym_lit name: (sym_name) @local.definition.parameter)))
 (:any-of? @_defn_head "defn" "defn-" "defmacro" "defmethod")
 (:not-eq? @local.definition.parameter "&"))
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
 (:any-of? @_defn_head "defn" "defn-" "defmacro" "defmethod")
 (:not-eq? @local.definition.parameter "&"))
((list_lit
   .
   (sym_lit name: (sym_name) @_fn_head)
   .
   (vec_lit
     (sym_lit name: (sym_name) @local.definition.parameter)))
 (:any-of? @_fn_head "fn" "fn*")
 (:not-eq? @local.definition.parameter "&"))
((list_lit
   .
   (sym_lit name: (sym_name) @_fn_head)
   .
   (sym_lit)
   .
   (vec_lit
     (sym_lit name: (sym_name) @local.definition.parameter)))
 (:any-of? @_fn_head "fn" "fn*")
 (:not-eq? @local.definition.parameter "&"))

#; Binding-vector names. The vector is captured WHOLE and its names are read
#; off it pairwise in code (Mode.cpp's ExpandPairwiseBindings) -- a query
#; pattern has no quantifier, so the only way to write this declaratively is
#; one pattern per even index, anchored `. (_) . (_)` per preceding pair,
#; which is what this file used to do and why it stopped at eight pairs. A
#; ninth binding was silently unresolvable.
#;
#; What stays here is the language knowledge: WHICH heads bind pairwise. What
#; moved is the quantifier, which was never Lisp-specific at all.
#;
#; A value that is itself a bare symbol (`[alias name]`) is still never
#; mistaken for a name -- the expansion counts elements rather than matching
#; them, so parity does the work the anchors used to. Destructuring is still
#; declined rather than guessed at (a form with children of its own is not a
#; name), and a comment between pairs no longer shifts parity, which the
#; anchored patterns got wrong.
((list_lit
   .
   (sym_lit name: (sym_name) @_bind_head)
   .
   (vec_lit) @local.definition.var.pairs)
 (:any-of? @_bind_head
   "let" "let*" "loop" "loop*" "when-let" "if-let" "when-some" "if-some"
   "doseq" "for" "with-open" "with-local-vars" "binding"))

#; Children that are not elements, and so must not shift the pairing.
#;
#; `#_form` is the reader's discard -- it reads as an ordinary child and means
#; "pretend this is not here", which is exactly a parity shift. Nothing in C++
#; knows what a reader macro is, so the query says it.
#;
#; A comment is here for a reason worth recording: the expansion already asks
#; the PARSER which children are extras (Node::IsExtra), which is the
#; language-agnostic form of this question and is how Janet's comments are
#; handled. But tree-sitter-clojure declares `extras: []` -- no extras at all,
#; comments are ordinary rules in its grammar -- so the generic mechanism has
#; nothing to report here and the query has to name it. Checked in
#; grammar.json rather than assumed after a binding after a comment stopped
#; resolving.
(dis_expr) @local.skip
(comment) @local.skip

#; References. Only the name half of an UNQUALIFIED symbol: `!namespace`
#; excludes `clojure.string/upper-case`, whose sym_name is `upper-case` but
#; which cannot be a local, and taking `name:` rather than the whole sym_lit
#; is what keeps a rename rewriting the bare name only.
(sym_lit
  !namespace
  name: (sym_name) @local.reference)
