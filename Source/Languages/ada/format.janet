# Format captures (see Docs/FormattingRules.md for each name's pass). Each
# library unit is its own compilation_unit, after its `with` clauses; the
# subprograms of a package (or nested in a subprogram) are its methods.

(compilation_unit
  [(subprogram_body) (package_body) (package_declaration) (subprogram_declaration)] @def.toplevel)
(compilation
  .
  (compilation_unit
    .
    [(subprogram_body) (package_body) (package_declaration) (subprogram_declaration)] @def.toplevel.first))

(non_empty_declarative_part (subprogram_body) @def.method)
(non_empty_declarative_part . (subprogram_body) @def.method.first)
