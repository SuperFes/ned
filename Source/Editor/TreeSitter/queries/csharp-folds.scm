; Hand-written for Ned's generic-code-folding feature -- see c-folds.scm's
; own header comment for why (no upstream/nvim-treesitter/Neovim-core
; folds.scm exists for C# either). Checked against
; tree-sitter/tree-sitter-c-sharp's own src/node-types.json/grammar.js
; directly: "declaration_list" covers class/struct/interface/record/
; namespace bodies (one shared node type across all of them, unlike C++'s
; own split between field_declaration_list and namespace's declaration_
; list); "block" covers method/statement bodies; "enum_member_declaration_
; list" covers enum bodies; "switch_body" covers a switch STATEMENT's arm
; list, while "switch_expression" is captured directly (grammar.js's own
; _switch_expression_body rule is underscore-prefixed -- hidden, never a
; real node -- so a switch EXPRESSION's own "{"/"}" tokens are direct
; children of switch_expression itself, same shape go-folds.scm's own
; header comment establishes for Go's analogous case); "accessor_list"
; covers a property/indexer/event's own get/set (or add/remove) block;
; "initializer_expression" covers an object/collection initializer's own
; brace-delimited element list.
(declaration_list) @fold
(enum_member_declaration_list) @fold
(block) @fold
(switch_body) @fold
(switch_expression) @fold
(accessor_list) @fold
(initializer_expression) @fold
