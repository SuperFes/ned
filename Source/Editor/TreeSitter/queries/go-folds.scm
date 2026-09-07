; Hand-written for Ned's generic-code-folding feature -- see c-folds.scm's
; own header comment for why (no upstream/nvim-treesitter/Neovim-core
; folds.scm exists for Go either). Checked against tree-sitter/tree-sitter-go's
; own grammar.js/node-types.json directly: "block" covers func/if/for bodies;
; "field_declaration_list" covers struct fields; unlike C-family grammars, Go
; has no wrapping block-equivalent node for switch/select/interface bodies --
; expression_switch_statement/type_switch_statement/select_statement/
; interface_type are themselves the brace-delimited node (the "switch x {"/
; "select {"/"interface {" header line and the case/method list share one
; node), so each is captured directly rather than via a child. "literal_value"
; covers a composite literal's own brace-delimited element list (struct/
; slice/map/array literals), the same role rust-folds.scm's own
; field_initializer_list plays for Rust.
(block) @fold
(field_declaration_list) @fold
(interface_type) @fold
(expression_switch_statement) @fold
(type_switch_statement) @fold
(select_statement) @fold
(literal_value) @fold
