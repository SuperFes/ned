# ned-authored, Delphi/Free Pascal layout. Pascal's keywords are named nodes
# (kBegin, kEnd, ...), which the imprint -- pairing anonymous tokens -- cannot
# see, so the keyword-bracketed constructs are named here: interior one level
# in, closing keyword back at the construct's own level.
[(block) (repeat) (try) (case) (asm)] @indent.headed
(block (kEnd) @dedent)
(asm (kEnd) @dedent)
(case (kEnd) @dedent)
(repeat (kUntil) @dedent)
(try [(kExcept) (kFinally) (kEnd)] @dedent)

# Declaration sections and a class's visibility sections. A class body's
# members sit one level in; with sections, `private` stays at the class's
# own level and the members sit under it.
[(declUses) (declTypes) (declVars) (declConsts) (declLabels) (declSection)] @indent.headed
(declClass [(declField) (declProc) (declProp)] @indent)
(declClass (kEnd) @dedent)

# A lone statement after then/else/do goes one level in; a begin...end block
# there stays at the statement's level (its own body is the indent), and so
# does an `else if` chain.
(_ body: [(statement) (assignment) (varDef) (if) (ifElse) (while) (repeat) (for) (foreach) (try) (case) (with) (raise) (goto)] @indent)
(_ then: [(statement) (assignment) (varDef) (while) (repeat) (for) (foreach) (try) (case) (with) (raise) (goto)] @indent)
(_ else: [(statement) (assignment) (varDef) (while) (repeat) (for) (foreach) (try) (case) (with) (raise) (goto)] @indent)
