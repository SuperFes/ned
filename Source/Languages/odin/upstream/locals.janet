# Scopes

[
  (block)
  (declaration)
  (statement)
] @scope

# References

(identifier) @reference

# Definitions

(package_declaration (identifier) @definition.namespace)

(import_declaration alias: (identifier) @definition.namespace)

(procedure_declaration (identifier) @definition.function)

(struct_declaration (identifier) @definition.type "::")

(enum_declaration (identifier) @definition.enum "::")

(union_declaration (identifier) @definition.type "::")

(bit_field_declaration (identifier) @definition.type "::")

(variable_declaration (identifier) @definition.var ":=")

(const_declaration (identifier) @definition.constant "::")

(const_type_declaration (identifier) @definition.type ":")

# ned: upstream's optional ":" token is dropped (a quantified bare token is
# outside QueryMatcher's measured scope; the definition is the same).
(parameter (identifier) @definition.parameter)

(default_parameter (identifier) @definition.parameter ":=")

(field (identifier) @definition.field ":")

(label_statement (identifier) @definition ":")
