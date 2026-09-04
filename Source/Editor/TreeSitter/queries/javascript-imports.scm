; import-target-tree-sitter follow-up: checked against tree-sitter-
; javascript's own node-types.json. import_statement/export_statement's
; "source:" field is a "string" node whose real content lives in a nested
; string_fragment child -- capturing that directly (rather than the
; delimiter-including "string" node itself) needs no quote-stripping.
; require(...) is matched via a #eq? predicate on the callee identifier
; (Query.h already evaluates this) since CommonJS require has no dedicated
; grammar node of its own, just an ordinary call_expression.
(import_statement source: (string (string_fragment) @import.target)) @import.statement
(export_statement source: (string (string_fragment) @import.target)) @import.statement
(call_expression
  function: (identifier) @_callee
  arguments: (arguments (string (string_fragment) @import.target))
  (#eq? @_callee "require")) @import.statement
; go-to-file-at-point resolver gaps follow-up: dynamic import("./foo") --
; checked against tree-sitter-javascript's own node-types.json, call_expression's
; "function:" field accepts a dedicated (unfielded) "import" node for exactly
; this syntax, distinct from an ordinary (identifier) callee, so no #eq?
; predicate is needed the way require(...) above needs one.
(call_expression
  function: (import)
  arguments: (arguments (string (string_fragment) @import.target))) @import.statement
