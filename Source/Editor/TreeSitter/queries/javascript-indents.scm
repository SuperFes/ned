; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. Checked against tree-sitter-javascript's own
; node-types.json.
(statement_block) @indent
(object) @indent
(object_pattern) @indent
(array) @indent
(array_pattern) @indent
(arguments) @indent
(formal_parameters) @indent
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
