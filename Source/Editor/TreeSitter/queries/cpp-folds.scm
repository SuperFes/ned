; Hand-written for Ned's generic-code-folding feature -- see c-folds.scm's
; own header comment for why (no upstream/nvim-treesitter/Neovim-core
; folds.scm exists for C++ either). compound_statement covers function/
; method bodies and control-flow blocks; field_declaration_list covers
; class/struct member lists.
(compound_statement) @fold
(field_declaration_list) @fold
