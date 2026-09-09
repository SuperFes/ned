; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention, and kotlin-folds.scm's for why this language's
; brace-delimited nodes are the ones they are (a hidden "_block" rule inlines
; its braces into the parent, so "statements" is the wrong node to key on and
; the sometimes-unbraced parents need an explicit "{" child). Mirrors that
; file's node selection. "function_value_parameters" (a function's own
; parameter list), "value_arguments" (a call's own arguments) and
; "indexing_suffix" (an "arr[i]" subscript) get @aligned rather than @indent,
; same reasoning c-indents.scm's own parameter_list/argument_list do.
(class_body) @indent
(enum_class_body) @indent
(lambda_literal) @indent
(when_expression) @indent
(anonymous_initializer) @indent
(catch_block) @indent
(finally_block) @indent
(function_body "{") @indent
(control_structure_body "{") @indent
(secondary_constructor "{") @indent
(function_value_parameters) @aligned
(value_arguments) @aligned
(indexing_suffix) @aligned

(class_body "}" @dedent)
(enum_class_body "}" @dedent)
(lambda_literal "}" @dedent)
(when_expression "}" @dedent)
(anonymous_initializer "}" @dedent)
(catch_block "}" @dedent)
(finally_block "}" @dedent)
(function_body "}" @dedent)
(control_structure_body "}" @dedent)
(secondary_constructor "}" @dedent)
(function_value_parameters ")" @dedent)
(value_arguments ")" @dedent)
(indexing_suffix "]" @dedent)
