# ned-authored. The imprint indents keyword bodies up to their `end`. A clause
# continuing one -- elsif/else, when/in, rescue/ensure -- sits at the
# construct's own level (crystal tool format) with its body one level in: the
# construct's interior stops at its first clause, and each clause is headed --
# its lines after the first, including an empty body's first line, go one in.
(_ [(elsif) (else) (when) (in) (rescue) (ensure)] @indent.end) @indent
[(elsif) (else) (when) (in) (rescue) (ensure)] @indent.headed
