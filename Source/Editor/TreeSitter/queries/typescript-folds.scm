; Hand-written for Ned's generic-code-folding feature -- see c-folds.scm's
; own header comment for why. tree-sitter-typescript's grammar extends
; tree-sitter-javascript's, reusing the same statement_block/object/
; class_body node names -- shared by both TypeScriptMode and TsxMode, same
; as this project's own kTypeScript highlight query already is (Mode.h).
(statement_block) @fold
(object) @fold
(class_body) @fold
