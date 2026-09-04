; Hand-written for Ned's generic-code-folding feature -- see c-folds.scm's
; own header comment for why (no upstream/nvim-treesitter/Neovim-core
; folds.scm exists for Rust either). Checked against tree-sitter-rust's own
; node-types.json: "block" covers fn/if/for/while/loop bodies,
; "declaration_list" covers impl/trait/mod bodies, "field_declaration_list"
; covers struct fields, "enum_variant_list" covers enum variants,
; "match_block" covers a match expression's arm list, and
; "field_initializer_list" covers a struct-literal's own field list.
(block) @fold
(declaration_list) @fold
(field_declaration_list) @fold
(enum_variant_list) @fold
(match_block) @fold
(field_initializer_list) @fold
