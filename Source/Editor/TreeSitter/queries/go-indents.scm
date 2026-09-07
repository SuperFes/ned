; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention -- checked against tree-sitter/tree-sitter-go's own
; grammar.js/node-types.json directly, same discipline rust-indents.scm's own
; header comment establishes, not assumed. Mirrors go-folds.scm's own node
; selection (see that file's header comment for why
; expression_switch_statement/type_switch_statement/select_statement/
; interface_type are each captured directly rather than via a child block --
; Go's switch/select/interface bodies have no separate wrapping node the way
; a func/if/for body's "block" does). "parameter_list" is Go's own node for a
; function/method's parameters AND its multi-value return type AND a method's
; receiver -- all three get @aligned for the same reason c-indents.scm's own
; parameter_list/argument_list do (a wrapped continuation line lines up under
; the first entry's own column, falling back to a plain indent when the
; opener is alone on its line).
(block) @indent
(field_declaration_list) @indent
(interface_type) @indent
(expression_switch_statement) @indent
(type_switch_statement) @indent
(select_statement) @indent
(literal_value) @indent
(parameter_list) @aligned
(argument_list) @aligned

(block "}" @dedent)
(field_declaration_list "}" @dedent)
(interface_type "}" @dedent)
(expression_switch_statement "}" @dedent)
(type_switch_statement "}" @dedent)
(select_statement "}" @dedent)
(literal_value "}" @dedent)
(parameter_list ")" @dedent)
(argument_list ")" @dedent)
