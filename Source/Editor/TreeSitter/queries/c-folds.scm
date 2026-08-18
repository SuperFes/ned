; Hand-written for Ned's generic-code-folding feature -- no upstream grammar
; or nvim-treesitter/Neovim-core query set ships a folds.scm for C (checked
; directly against tree-sitter/tree-sitter-c and neovim/neovim's own
; runtime/queries, not assumed). Deliberately minimal for v1: every brace-
; delimited compound statement (function bodies, if/for/while/switch blocks,
; ...) is foldable. Struct/enum/union bodies are covered separately by C++'s
; own field_declaration_list capture -- plain C's grammar represents those
; as field_declaration_list too, but this file intentionally doesn't add it
; yet, matching the "start with a few, extend later" scope this feature
; shipped with.
(compound_statement) @fold
