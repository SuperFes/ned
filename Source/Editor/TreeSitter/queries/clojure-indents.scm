; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention, and janet-indents.scm's own comment for why this is
; deliberately bracket-depth only, not real per-form semantic Lisp indent.
; Shared by ClojureMode and JankMode (same grammar), mirroring kClojure's own
; sharing. Checked against tree-sitter-clojure's own node-types.json.
(list_lit) @indent
(vec_lit) @indent
(map_lit) @indent
(set_lit) @indent
(anon_fn_lit) @indent

(list_lit ")" @dedent)
(vec_lit "]" @dedent)
(map_lit "}" @dedent)
(set_lit "}" @dedent)
(anon_fn_lit ")" @dedent)
