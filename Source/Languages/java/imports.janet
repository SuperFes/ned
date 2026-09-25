# `import a.b.C;` names C's file; `import static a.b.C.m;` names C's too.
# A wildcard names a package, which is no one file.
(import_declaration
  (scoped_identifier) @import.module
  (:not-match? @import.statement "^import\\s+static\\s")
  (:not-match? @import.statement "\\*")) @import.statement
(import_declaration
  (scoped_identifier scope: (_) @import.module)
  (:match? @import.statement "^import\\s+static\\s")
  (:not-match? @import.statement "\\*")) @import.statement
