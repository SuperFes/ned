(import_header (identifier) @import.module .) @import.statement
(import_header (identifier) @import.module . (import_alias)) @import.statement

#; The file's own package, which a move rewrites.
(package_header (identifier) @import.package) @import.statement
