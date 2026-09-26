# ned-authored. The imprint indents object bodies, lists and parameter
# lists; this adds continuation lines: a property or method whose value
# follows `=` on later lines, and a wrapped operator expression. The
# continuation starts at the `=` -- a declaration's node also spans its doc
# comments and annotations above it. `if`/`let` are left alone: their
# `else` and body line up with the keyword.
(classProperty "=" @indent.begin) @indent.continuation
(objectProperty "=" @indent.begin) @indent.continuation
(classMethod "=" @indent.begin) @indent.continuation
[(additiveExpr) (multiplicativeExpr) (exponentiationExpr) (comparisonExpr) (equalityExpr)
 (logicalAndExpr) (logicalOrExpr) (nullCoalesceExpr) (pipeExpr)] @indent.continuation
