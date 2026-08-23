; import-target-tree-sitter follow-up: sogaiu/tree-sitter-clojure is a
; generic reader-level grammar (list_lit/sym_lit/str_lit, no semantic
; "require-form" node of its own -- checked against its own node-types.json),
; so this matches the common top-level "(require 'foo.bar)"/
; "(require \"foo.bar\")"/"(import foo.bar)"/"(import 'foo.bar)" call shapes
; by their own leading symbol's text via a #any-of?/#eq? predicate. Shared by
; ClojureMode and JankMode, same sharing queries::kClojure's highlight query
; already uses.
;
; A namespace symbol munges dots (and, in real Clojure, hyphens too) to a
; classpath-relative path the same way Python's dotted modules do --
; @import.module signals the dot-to-slash half of that; the hyphen/underscore
; half is a deliberate v1 imprecision, not attempted here.
;
; The ns-form's own "(:require [foo.bar :as fb])" vector shape inside a
; top-level (ns ...) declaration is deliberately not matched -- reliably
; distinguishing a :require *keyword key* from an ordinary keyword at this
; grammar's level of abstraction needs more positional bookkeeping than a
; declarative query buys here; a v1 cut, revisit if it bites.
(list_lit
  .
  (sym_lit name: (sym_name) @_head)
  .
  (quoting_lit value: (sym_lit name: (sym_name) @import.module))
  (#any-of? @_head "require" "use")) @import.statement
(list_lit
  .
  (sym_lit name: (sym_name) @_head)
  .
  (sym_lit name: (sym_name) @import.module)
  (#eq? @_head "import")) @import.statement
(list_lit
  .
  (sym_lit name: (sym_name) @_head)
  .
  (quoting_lit value: (sym_lit name: (sym_name) @import.module))
  (#eq? @_head "import")) @import.statement
(list_lit
  .
  (sym_lit name: (sym_name) @_head)
  .
  (str_lit) @import.target
  (#any-of? @_head "require" "use" "load")) @import.statement
