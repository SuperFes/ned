# formatter-rules follow-up: same Lisp-family reasoning as clojure/
# format.janet's own header comment (read that first) -- def.toplevel
# only, via head-symbol-text matching, nothing for Space/Break at all.
# Janet's own grammar is even more minimal than Clojure's: EVERY node
# type here (`par_tup_lit`, `sqr_tup_lit`, `sym_lit`, ...) has an empty
# `fields` list -- confirmed via node-types.json -- so there is no
# `value:`/`name:` field to route through the way Clojure's `sym_lit`
# still has (a `name: (sym_name)` child). `sym_lit` is a LEAF node whose
# own byte span IS the symbol's text, so the head symbol is captured and
# matched directly, anchored `.` to the tuple's own first position (no
# field to require it through).
#
# `(#match?`-style predicates DO NOT WORK in this codebase's own .janet
# query files -- caught live, not assumed: a bare leading "#" is read as
# a genuine Janet line-comment marker by the no-VM reader
# (`JanetData.h`) even mid-token, so `(#match? ...)` silently truncates
# the rest of the pattern rather than erroring at the predicate layer.
# The established convention (confirmed by grepping existing csharp/
# kotlin/bash locals.janet usage before writing this) is the COLON form:
# `:match?`/`:eq?`.
#
# def/defn/defmacro/defn-/def- all start with "def", the same "starts
# with def" heuristic clojure/format.janet uses (and real Clojure/Janet
# formatters use themselves) -- one `:match? "^def"` pattern covers all
# of them with no per-form enumeration. `var`/`var-` (Janet's own
# top-level MUTABLE binding form, real and reasonably common, unlike
# Clojure which has no close equivalent) don't start with "def", so
# they're checked separately via `:match? "^var"` -- verified live
# neither check falsely fires on an ordinary function call or a nested
# `let`/`def` inside one (Janet's own `let` is itself just another
# `par_tup_lit` whose head is "let", never touching this file's own
# top-level-only `source`-anchored patterns).
#
# No def.method: Janet has no protocol/record/method-nesting construct
# analogous to Clojure's defprotocol/defrecord at all (structs/tables are
# plain data, never containers of nested def-shaped forms) -- a real
# absence, not a scope cut.
(source . (par_tup_lit . (sym_lit) @head (:match? @head "^def")) @def.toplevel.first)
(source (par_tup_lit . (sym_lit) @head (:match? @head "^def")) @def.toplevel)
(source . (par_tup_lit . (sym_lit) @varhead (:match? @varhead "^var")) @def.toplevel.first)
(source (par_tup_lit . (sym_lit) @varhead (:match? @varhead "^var")) @def.toplevel)
