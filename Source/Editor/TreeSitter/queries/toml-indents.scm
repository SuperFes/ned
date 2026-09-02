; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. TOML's [table]/[[array-of-tables]] headers don't wrap
; their following pairs in the parse tree at all (TOML's structure is flat
; past the header line), so only real bracketed containers -- multi-line
; arrays and inline tables -- have anything to indent. Checked against
; tree-sitter-toml's own node-types.json.
(array) @indent
(inline_table) @indent

(array "]" @dedent)
(inline_table "}" @dedent)
