; import-target-tree-sitter follow-up: checked against tree-sitter-python's
; own node-types.json. import_statement's own "name:" field ("import a.b,
; c.d") deliberately gets no @import.statement of its own -- each
; dotted_name's own range is the resolvable range, since widening to the
; whole line would be ambiguous once there's more than one name on it.
; import_from_statement's "module_name:" field does get one, since "from a.b
; import c, d" only ever has one real target regardless of which imported
; name point lands on. Leading-dot relative imports (module_name:
; (relative_import), "from . import x") are deliberately not matched --
; resolving them needs package-root-boundary detection this project doesn't
; have (v1 cut, see ROADMAP.md).
(import_statement name: (dotted_name) @import.module)
(import_statement name: (aliased_import name: (dotted_name) @import.module))
(import_from_statement module_name: (dotted_name) @import.module) @import.statement
