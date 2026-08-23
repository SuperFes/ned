; import-target-tree-sitter follow-up: checked against tree-sitter-css's own
; node-types.json. import_statement's target child is unfielded (its real
; children vary -- string_value, a call_expression for url(...), plus
; trailing media-query nodes) so this matches by node type rather than field
; name; low false-positive risk since a media-query condition after the URL
; uses identifiers/numbers, not strings.
(import_statement (string_value (string_content) @import.target)) @import.statement
(import_statement (call_expression (arguments (string_value (string_content) @import.target)))) @import.statement
(import_statement (call_expression (arguments (plain_value) @import.target))) @import.statement
