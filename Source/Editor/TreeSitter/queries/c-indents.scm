; smart-indentation follow-up. Hand-written, ned-local "indent"/"dedent"
; capture-name convention (Editor/Indent.h's generic tree-walk engine) --
; borrowed from nvim-treesitter/Helix as capture NAMES only, no upstream
; indents.scm exists for C to vendor (checked directly against
; tree-sitter/tree-sitter-c and neovim/neovim's own runtime/queries, the same
; "hand-written, checked, not assumed" precedent c-folds.scm's own header
; comment establishes). compound_statement covers function/control-flow
; bodies; field_declaration_list covers struct/union member lists (a
; genuinely different scope-cut than c-folds.scm's own deliberate omission of
; it -- that was a folding-affordance decision, not an indentation one:
; struct members still need to indent one level regardless of whether the
; struct body itself is foldable). initializer_list/parameter_list/
; argument_list cover multi-line brace-initializers and wrapped declaration/
; call parameter lists.
(compound_statement) @indent
(field_declaration_list) @indent
(initializer_list) @indent
(parameter_list) @indent
(argument_list) @indent

(compound_statement "}" @dedent)
(field_declaration_list "}" @dedent)
(initializer_list "}" @dedent)
(parameter_list ")" @dedent)
(argument_list ")" @dedent)
