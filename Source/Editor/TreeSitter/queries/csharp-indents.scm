; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention -- checked against tree-sitter/tree-sitter-c-sharp's own
; src/node-types.json/grammar.js directly, same discipline every other
; *-indents.scm in this project holds to. Mirrors csharp-folds.scm's own node
; selection (see that file's header comment for the declaration_list/
; switch_body/switch_expression/accessor_list/initializer_expression
; reasoning). "parameter_list"/"argument_list" (a method's own parameters/a
; call's own arguments) and their bracketed siblings "bracketed_parameter_
; list"/"bracketed_argument_list" (an indexer's "this[...]" and an element
; access's "arr[...]") all get @aligned rather than @indent, same reasoning
; c-indents.scm's own parameter_list/argument_list do.
(declaration_list) @indent
(enum_member_declaration_list) @indent
(block) @indent
(switch_body) @indent
(switch_expression) @indent
(accessor_list) @indent
(initializer_expression) @indent
(parameter_list) @aligned
(argument_list) @aligned
(bracketed_parameter_list) @aligned
(bracketed_argument_list) @aligned

(declaration_list "}" @dedent)
(enum_member_declaration_list "}" @dedent)
(block "}" @dedent)
(switch_body "}" @dedent)
(switch_expression "}" @dedent)
(accessor_list "}" @dedent)
(initializer_expression "}" @dedent)
(parameter_list ")" @dedent)
(argument_list ")" @dedent)
(bracketed_parameter_list "]" @dedent)
(bracketed_argument_list "]" @dedent)
