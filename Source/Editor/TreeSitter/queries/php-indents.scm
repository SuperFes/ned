; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. Checked against tree-sitter-php's own node-types.json.
(compound_statement) @indent
(declaration_list) @indent
(array_creation_expression) @indent
(arguments) @indent
(formal_parameters) @indent
(switch_block) @indent
(match_block) @indent

(compound_statement "}" @dedent)
(declaration_list "}" @dedent)
(array_creation_expression "]" @dedent)
(array_creation_expression ")" @dedent)
(arguments ")" @dedent)
(formal_parameters ")" @dedent)
(switch_block "}" @dedent)
(match_block "}" @dedent)
