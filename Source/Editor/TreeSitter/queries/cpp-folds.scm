; Hand-written for Ned's generic-code-folding feature -- see c-folds.scm's
; own header comment for why (no upstream/nvim-treesitter/Neovim-core
; folds.scm exists for C++ either). compound_statement covers function/
; method bodies and control-flow blocks; field_declaration_list covers
; class/struct member lists.
(compound_statement) @fold
(field_declaration_list) @fold
;; namespace/extern-"C" bodies. Added after Tier 0 imprint inference
;; (Editor/Imprint.h) reported it on a real file and the hand-written list
;; did not -- C# and Rust already folded their own declaration_list, so this
;; was an omission here rather than a considered exclusion.
(declaration_list) @fold
