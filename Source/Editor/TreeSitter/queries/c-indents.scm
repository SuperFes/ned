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
; struct body itself is foldable). initializer_list covers multi-line
; brace-initializers. parameter_list/argument_list (@aligned-paren-column-
; alignment follow-up) get "aligned" rather than "indent" -- a wrapped
; declaration/call's continuation lines conventionally line up under the
; first parameter/argument's own column (e.g. "foo(a,\n    b)"), not one
; flat indent level deeper; Editor/Indent.h's engine falls back to plain
; @indent behavior automatically when the opener is alone on its own line
; (nothing to align to), so this loses nothing for that shape.
(compound_statement) @indent
(field_declaration_list) @indent
(initializer_list) @indent
(parameter_list) @aligned
(argument_list) @aligned

(compound_statement "}" @dedent)
(field_declaration_list "}" @dedent)
(initializer_list "}" @dedent)
(parameter_list ")" @dedent)
(argument_list ")" @dedent)
