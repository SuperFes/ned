# ned-authored. The imprint indents keyword bodies up to their `end`. A clause
# continuing one -- elsif/else, when/in, rescue/ensure -- sits at the
# construct's own level (crystal tool format) with its body one level in: the
# construct's interior stops at its first clause, and each clause is headed --
# its lines after the first, including an empty body's first line, go one in.
(_ [(elsif) (else) (when) (in) (rescue) (ensure)] @indent.end) @indent
[(elsif) (else) (when) (in) (rescue) (ensure)] @indent.headed

# Continuation lines -- see c-indents.scm. A binary operator is a `call`
# here, the same node as a method chain.
[(assign) (call) (and) (or) (return)] @indent.continuation
# `a = case x` / `a = if x`: the construct's own lines sit at the statement's
# level (RuboCop's variable-aligned `end`), not a continuation step in.
[(assign (case)) (assign (if)) (assign (unless)) (assign (while)) (assign (until))
 (assign (begin))] @indent.suppress

# A macro body is template text the macro pastes as written; only the
# `{{ }}`/`{% %}` pieces inside it are Crystal the parser reads.
(macro_content) @indent.ignore
