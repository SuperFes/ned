; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention -- this is C's own indent query plus C++-only container
; node types: declaration_list covers namespace bodies (C++ doesn't reuse
; field_declaration_list for those the way it does for class/struct/union
; bodies).
(compound_statement) @indent
(field_declaration_list) @indent
(declaration_list) @indent
(initializer_list) @indent
(parameter_list) @indent
(argument_list) @indent

(compound_statement "}" @dedent)
(field_declaration_list "}" @dedent)
(declaration_list "}" @dedent)
(initializer_list "}" @dedent)
(parameter_list ")" @dedent)
(argument_list ")" @dedent)
