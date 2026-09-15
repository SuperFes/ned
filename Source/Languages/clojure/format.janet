# formatter-rules follow-up: Lisp-family, a genuinely different template
# from every language before it -- confirmed live before writing anything
# (a real sexp dump plus throwaway `tree-sitter query` probes) rather than
# assumed to need one just because the doc's own earlier note predicted
# it. Every construct in this grammar -- `def`, `defn`, `ns`, an ordinary
# function call, a `let` binding form -- is the SAME node type
# (`list_lit`), discriminated by nothing structural at all: there is no
# `body:`/`condition:` field anywhere to hang a capture on the way even
# Kotlin's zero-field grammar still had (Kotlin's own ambiguity was
# node-type reuse WITHIN one construct; this grammar has no per-construct
# node types to begin with).
#
# Consequence: `brace.function`/`brace.control`/`brace.class`/`control.
# parens` are all genuinely inapplicable, not declined for lack of
# trying -- there is no separate "block-opening delimiter" distinct from
# the list itself for :placement to move (Lisp code never debates
# same-line-vs-next-line parens the way C-family debates brace style),
# and no optional-parens lever the way Python/Go/Lua's own files have
# (every form is ALREADY parenthesized, unconditionally). This file names
# none of Space/Break's own captures at all.
#
# def.toplevel IS real, via a NEW technique this rollout hasn't needed
# before: matching the list's own FIRST child symbol's TEXT rather than
# any field or node type, the same "dispatch on the head symbol" approach
# already proven for this language's own locals.scm/tags.scm (a Lisp
# grammar carries no semantic structure at all -- see Mode.h's own doc
# comment on LocalScopeFunction for the precedent). `(:match? @head
# "^def")` catches def/defn/defn-/defmacro/defprotocol/defrecord/
# deftype/defmulti/defmethod uniformly with no per-macro enumeration --
# the same "starts with def" heuristic cljfmt (the real Clojure
# community formatter) itself uses, not an invented cut. `ns` is checked
# separately (doesn't start with "def", but is the single most common
# top-level form actually wanting blank-line separation in real code).
# Verified live this does NOT falsely fire on an ordinary function call
# or a nested `let` binding (neither's head symbol matches either check).
#
# def.method is deliberately NOT attempted: confirmed live via a real
# parse dump that `defprotocol`/`defrecord`'s own nested method
# signatures (`(area [this])`) are ordinary `list_lit` children with
# ARBITRARY head symbols (the method's own name) -- there is no
# vocabulary-based signal here the way "^def" is for def.toplevel, only
# a position-and-container-dependent one (a list nested directly inside
# a defprotocol/defrecord/deftype/extend-protocol form, after their own
# leading symbols) that would need per-macro-shape special-casing this
# codebase's own "decline rather than approximate a fragile signal"
# precedent argues against, for a narrow JetBrains-style preference
# (blank lines between protocol method signatures) not worth that risk.
# A real, honest absence, not a scope cut -- matching Go's own "no
# def.method" precedent, for an analogous reason (no reliable signal).
(source . (list_lit value: (sym_lit name: (sym_name) @head) (:match? @head "^def")) @def.toplevel.first)
(source (list_lit value: (sym_lit name: (sym_name) @head) (:match? @head "^def")) @def.toplevel)
(source . (list_lit value: (sym_lit name: (sym_name) @nshead) (:eq? @nshead "ns")) @def.toplevel.first)
(source (list_lit value: (sym_lit name: (sym_name) @nshead) (:eq? @nshead "ns")) @def.toplevel)
