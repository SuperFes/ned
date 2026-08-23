; import-target-tree-sitter follow-up: checked against tree-sitter-
; typescript's own node-types.json for both its typescript/ and tsx/
; grammars (identical shape for the nodes referenced here) -- shared by
; TypeScriptMode and TsxMode, the same sharing kTypeScript's highlight query
; already uses (see Mode.h's TsxMode doc comment). Same import_statement/
; export_statement/require(...) shapes as javascript-imports.scm
; (TypeScript's grammar is a structural superset of JavaScript's for these
; nodes) plus TypeScript's own "import foo = require(...)" form.
(import_statement source: (string (string_fragment) @import.target)) @import.statement
(export_statement source: (string (string_fragment) @import.target)) @import.statement
(import_require_clause source: (string (string_fragment) @import.target)) @import.statement
(call_expression
  function: (identifier) @_callee
  arguments: (arguments (string (string_fragment) @import.target))
  (#eq? @_callee "require")) @import.statement
