; Hand-written for Ned's generic-code-folding feature -- see c-folds.scm's
; own header comment for why (no upstream/nvim-treesitter/Neovim-core
; folds.scm exists for Java either). Checked against tree-sitter/
; tree-sitter-java's own src/node-types.json/grammar.js directly: unlike C#'s
; single shared "declaration_list", Java gives each declaring construct its
; own body node -- "class_body" (class and record), "interface_body",
; "enum_body", "annotation_type_body" (an "@interface" declaration's own
; members) and "module_body" (a module-info.java directive list). "block"
; covers every method/statement body, with "constructor_body" its separate
; sibling (a constructor's body is its own node because it may open with an
; explicit_constructor_invocation, which a plain block can't hold).
; "switch_block" covers both the classic "case x:" form and the arrow
; "case x ->" form -- one node type for both, so no switch_expression-style
; second capture is needed the way C#/Go need one. "array_initializer"
; covers a braced "{1, 2, 3}" array literal; "element_value_array_initializer"
; is its annotation-argument-only twin (@Foo({"a", "b"})), a distinct node
; type rather than a reuse of the former.
(class_body) @fold
(interface_body) @fold
(enum_body) @fold
(annotation_type_body) @fold
(module_body) @fold
(block) @fold
(constructor_body) @fold
(switch_block) @fold
(array_initializer) @fold
(element_value_array_initializer) @fold
