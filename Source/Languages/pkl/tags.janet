#; Symbol-kind query, ned-authored (tree-sitter-pkl ships none). Module-level
#; properties are the config's keys; one amended with an object body is a
#; container, and its own properties are the second level.

(moduleClause
  (qualifiedIdentifier) @name) @definition.module

(typeAlias
  (identifier) @name) @definition.type

(clazz
  (identifier) @name) @definition.class

(classBody
  (classProperty
    (identifier) @name) @definition.property)

(classBody
  (classMethod
    (methodHeader (identifier) @name)) @definition.method)

(module
  (classMethod
    (methodHeader (identifier) @name)) @definition.function)

(module
  (classProperty
    (identifier) @name
    (objectBody)) @definition.namespace)

(module
  (classProperty
    (identifier) @name
    "=") @definition.field)

(module
  (classProperty
    (objectBody
      (objectProperty
        (identifier) @name) @definition.field)))
