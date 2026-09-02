; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. Every object/array is an indent scope, mirroring
; json-folds.scm's own "every object/array" scope exactly.
(object) @indent
(array) @indent

(object "}" @dedent)
(array "]" @dedent)
