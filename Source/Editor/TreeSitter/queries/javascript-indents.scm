; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. Checked against tree-sitter-javascript's own
; node-types.json. arguments/formal_parameters (@aligned-paren-column-
; alignment follow-up) get "aligned" instead of "indent" -- see
; c-indents.scm's own comment on parameter_list/argument_list for why.
(statement_block) @indent
(object) @indent
(object_pattern) @indent
(array) @indent
(array_pattern) @indent
(arguments) @aligned
(formal_parameters) @aligned
(class_body) @indent
(switch_body) @indent

(statement_block "}" @dedent)
(object "}" @dedent)
(object_pattern "}" @dedent)
(array "]" @dedent)
(array_pattern "]" @dedent)
(arguments ")" @dedent)
(formal_parameters ")" @dedent)
(class_body "}" @dedent)
(switch_body "}" @dedent)
