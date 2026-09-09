; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention -- checked against tree-sitter/tree-sitter-java's own
; src/node-types.json/grammar.js directly, same discipline every other
; *-indents.scm in this project holds to. Mirrors java-folds.scm's own node
; selection (see that file's header comment for the per-construct body-node
; reasoning). "formal_parameters" (a method's own parameter list),
; "argument_list" (a call's own arguments) and "annotation_argument_list"
; (an annotation's own "(...)") get @aligned rather than @indent, same
; reasoning c-indents.scm's own parameter_list/argument_list do.
(class_body) @indent
(interface_body) @indent
(enum_body) @indent
(annotation_type_body) @indent
(module_body) @indent
(block) @indent
(constructor_body) @indent
(switch_block) @indent
(array_initializer) @indent
(element_value_array_initializer) @indent
(formal_parameters) @aligned
(argument_list) @aligned
(annotation_argument_list) @aligned

(class_body "}" @dedent)
(interface_body "}" @dedent)
(enum_body "}" @dedent)
(annotation_type_body "}" @dedent)
(module_body "}" @dedent)
(block "}" @dedent)
(constructor_body "}" @dedent)
(switch_block "}" @dedent)
(array_initializer "}" @dedent)
(element_value_array_initializer "}" @dedent)
(formal_parameters ")" @dedent)
(argument_list ")" @dedent)
(annotation_argument_list ")" @dedent)
