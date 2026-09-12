#; Local-binding query (the tree-sitter/Neovim "@local.scope"/
#; "@local.definition*"/"@local.reference" convention -- see
#; Mode::localScopes in Editor/Mode.h and Editor/LocalScopes.h for how these
#; captures are turned into a resolved binding).
#;
#; Three rules every locals query in this directory follows, each one load-
#; bearing rather than stylistic:
#;
#;   1. A scope capture must cover its own PARAMETER LIST, not just its body
#;      -- so it is the whole function_definition here, never the inner
#;      compound_statement. A body-only scope leaves every parameter outside
#;      every scope, i.e. indistinguishable from a file-level binding, and a
#;      file-level binding is exactly what rename-symbol refuses to touch.
#;
#;   2. The file root is deliberately NOT a scope. "No enclosing scope" is
#;      how LocalScopes.h recognizes a file-level binding, which can be
#;      referenced from other translation units and so is a language
#;      server's business, not this file's.
#;
#;   3. A definition capture is only for a name whose binding really is
#;      local. A function's own name is NOT captured: it sits inside the
#;      function_definition node that is its own scope, so capturing it
#;      would make the function look like a local of itself and rename it
#;      in this file alone. A reference to it then resolves to nothing and
#;      rename-symbol declines, which is the correct answer.
#;
#; File-level *variables* are captured (a top-level declaration sits outside
#; every scope, so rule 3 doesn't bite): reporting them as file-level is what
#; lets LocalScopes.h tell "shadowed by an outer binding" apart from "nothing
#; in this file binds this name at all".

#; Scopes
[
  (function_definition)
  (compound_statement)
  (for_statement)
] @local.scope

#; Parameters
(parameter_declaration
  declarator: (identifier) @local.definition.parameter)
(parameter_declaration
  declarator: (pointer_declarator
    declarator: (identifier) @local.definition.parameter))
(parameter_declaration
  declarator: (array_declarator
    declarator: (identifier) @local.definition.parameter))

#; Variables -- both the initialized and the bare form, through a pointer or
#; array declarator where there is one.
(declaration
  declarator: (identifier) @local.definition.var)
(declaration
  declarator: (init_declarator
    declarator: (identifier) @local.definition.var))
(declaration
  declarator: (pointer_declarator
    declarator: (identifier) @local.definition.var))
(declaration
  declarator: (init_declarator
    declarator: (pointer_declarator
      declarator: (identifier) @local.definition.var)))
(declaration
  declarator: (array_declarator
    declarator: (identifier) @local.definition.var))
(declaration
  declarator: (init_declarator
    declarator: (array_declarator
      declarator: (identifier) @local.definition.var)))

#; References. A struct/union member is a distinct field_identifier node and
#; a type name a type_identifier, so neither is swept up here.
(identifier) @local.reference
