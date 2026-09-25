# `import a.b.C` spells its path as sibling identifiers, not one node, so
# the declaration itself is trimmed down to it. Selectors and wildcards
# name a package, which is no one file.
(import_declaration
  path: (identifier) .
  (:match? @import.module "^import [^ ]")
  (:offset! @import.module 0 7 0 0)) @import.module

#; The file's own package, which a move rewrites.
(package_clause (package_identifier) @import.package) @import.statement
