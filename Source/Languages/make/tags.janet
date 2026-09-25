#; Symbol-kind query, ned-authored (tree-sitter-make ships none). Targets
#; are the outline; pattern rules (`%.o`) and special targets (`.PHONY`)
#; name no target of their own.

(rule
  (targets
    (word) @name
    (:not-match? @name "^\\.|%"))) @definition.function

#; A `+=` appends to a variable defined elsewhere, and a target-specific
#; assignment (`app: CFLAGS = -g`) belongs to its target.
([(makefile
   (variable_assignment
     .
     name: (word) @name
     operator: _ @_op) @definition.variable)
  (conditional
   (variable_assignment
     .
     name: (word) @name
     operator: _ @_op) @definition.variable)]
 (:not-eq? @_op "+="))

(define_directive
  name: (word) @name) @definition.macro
