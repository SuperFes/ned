; import-target-tree-sitter follow-up: checked against tree-sitter-php's own
; node-types.json. include/require take a single, unfielded expression child
; -- only the plain single-quoted-string case is matched (its "string_content"
; child, unquoted); a concatenated path (require __DIR__ . '/foo.php') or a
; double-quoted/interpolated string isn't a case this project's Link.cpp-style
; resolvers try to evaluate either, so it's left unmatched rather than
; guessed at. namespace_use_declaration ("use Foo\Bar;") is deliberately not
; matched -- a PHP namespace resolves to a file only via a framework's own
; PSR-4 autoloader config, which this project has no reason to parse.
(include_expression (string (string_content) @import.target)) @import.statement
(include_once_expression (string (string_content) @import.target)) @import.statement
(require_expression (string (string_content) @import.target)) @import.statement
(require_once_expression (string (string_content) @import.target)) @import.statement
