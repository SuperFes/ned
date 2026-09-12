; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. Every object/array is an indent scope, which is the
; same scope the deleted json-folds.scm took and the imprint now derives.
(object) @indent
(array) @indent

(object "}" @dedent)
(array "]" @dedent)
