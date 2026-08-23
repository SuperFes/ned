; import-target-tree-sitter follow-up: sogaiu/tree-sitter-janet-simple is a
; generic reader-level grammar (par_tup_lit/sym_lit/str_lit, no semantic
; "import-form" node of its own -- checked against its own node-types.json),
; matched the same way clojure-imports.scm matches Clojure's own generic
; grammar: by the operator symbol's own text via a #any-of? predicate.
; Janet's own sym_lit carries its text directly (no nested name field the
; way Clojure's does), and Janet module paths are typically already
; slash-separated (e.g. "spork/misc") rather than dotted, so the captured
; symbol is tagged @import.target (a literal-ish path ResolveFileLink's own
; extension inference can complete, e.g. "spork/misc" -> "spork/misc.janet"),
; not @import.module -- no dot-to-slash conversion applies here.
(par_tup_lit
  .
  (sym_lit) @_head
  .
  (sym_lit) @import.target
  (#any-of? @_head "import" "import*")) @import.statement
(par_tup_lit
  .
  (sym_lit) @_head
  .
  (str_lit) @import.target
  (#any-of? @_head "require" "dofile")) @import.statement
