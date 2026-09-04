; import-target-tree-sitter follow-up: checked against tree-sitter-php's own
; node-types.json. include/require take a single, unfielded expression child
; -- only the plain single-quoted-string case is matched (its "string_content"
; child, unquoted); a concatenated path (require __DIR__ . '/foo.php') or a
; double-quoted/interpolated string isn't a case this project's Link.cpp-style
; resolvers try to evaluate either, so it's left unmatched rather than
; guessed at.
;
; go-to-file-at-point resolver gaps follow-up: namespace_use_declaration
; ("use Foo\Bar;") is now matched too, tagged @import.namespace -- Mode.cpp's
; closure keeps its captured text (backslashes, no dot-to-slash conversion)
; verbatim, resolved by a dedicated PSR-4 lookup against composer.json
; (Editor/Php.h's ResolvePsr4Namespace), not ResolveFileLink's generic
; baseDirectory/ProjectRoot/includePaths search. Checked against
; tree-sitter-php's own grammar.js (node-types.json alone is ambiguous
; here): namespace_use_declaration always wraps each comma-separated name in
; its own namespace_use_clause -- a bare, un-namespaced "use Foo;" produces a
; (name), a real namespace "use Foo\Bar;" a (qualified_name); the
; group-import form ("use Foo\{Bar, Baz};") is deliberately not matched, a
; v1 cut, same precedent clojure-imports.scm's own doc comment already
; states for a different construct. A "use function foo\bar();"/"use const
; FOO\BAR;" clause matches the same way (this query has no way to test the
; clause's own optional "type:" field for absence) and simply won't resolve
; to a real file, the same lenient-false-positive tradeoff
; bash-imports.scm's source/"." command-name match already accepts.
(namespace_use_declaration
  (namespace_use_clause
    [(name) (qualified_name)] @import.namespace)) @import.statement
(include_expression (string (string_content) @import.target)) @import.statement
(include_once_expression (string (string_content) @import.target)) @import.statement
(require_expression (string (string_content) @import.target)) @import.statement
(require_once_expression (string (string_content) @import.target)) @import.statement
