#; Symbol-kind query. DerekStride/tree-sitter-sql ships none.

(create_table
  (object_reference
    name: (identifier) @name)) @definition.struct

(column_definition
  name: (identifier) @name) @definition.field

(create_view
  (object_reference
    name: (identifier) @name)) @definition.type

(create_materialized_view
  (object_reference
    name: (identifier) @name)) @definition.type

(create_type
  (object_reference
    name: (identifier) @name)) @definition.type

(create_function
  (object_reference
    name: (identifier) @name)) @definition.function

#; The trigger's own name, not the table after ON.
(create_trigger
  (keyword_trigger)
  .
  (object_reference
    name: (identifier) @name)) @definition.function

(create_schema
  (identifier) @name) @definition.namespace
