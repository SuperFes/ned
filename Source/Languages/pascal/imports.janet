# A unit is found by its name on the unit search path; a program's
# `uses X in 'x.pas'` also gives the file.
(declUses (moduleName) @import.module)
(declUses (literalString) @import.target)
