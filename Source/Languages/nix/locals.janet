#; Local-binding query, ned-authored after nvim-treesitter's (tree-sitter-nix
#; ships none) -- see c/locals.janet's header for the three rules every
#; locals query here follows.
#;
#; Nix binds lazily, so a `let` or `rec` name is visible to bindings written
#; before it; LocalScopes resolves a use that precedes its only definition
#; anyway, and declines when an outer binding makes that ambiguous.
#;
#; `inherit x;` copies the outer `x` in under the same name, so it stays a
#; use of that binding: renaming one renames both. `inherit (src) x;` binds
#; a new `x` from `src.x`, so it is a definition.

(let_expression) @local.scope
(rec_attrset_expression) @local.scope
(function_expression) @local.scope

(let_expression
  (binding_set
    (binding
      attrpath: (attrpath
        .
        attr: (identifier) @local.definition.var))))

(let_expression
  (binding_set
    (inherit_from
      attrs: (inherited_attrs
        attr: (identifier) @local.definition.var))))

(rec_attrset_expression
  (binding_set
    (binding
      attrpath: (attrpath
        .
        attr: (identifier) @local.definition.field))))

(function_expression
  universal: (identifier) @local.definition.parameter)

(formal
  name: (identifier) @local.definition.parameter)

(variable_expression
  name: (identifier) @local.reference)

(inherit
  attrs: (inherited_attrs
    attr: (identifier) @local.reference))
