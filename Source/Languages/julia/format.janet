# Format captures (see Docs/FormattingRules.md for each name's pass). A
# module's contents are top level: Julia doesn't indent them, and a file is
# usually one module.

(source_file
  [(function_definition) (macro_definition) (struct_definition) (abstract_definition)
   (primitive_definition) (module_definition)] @def.toplevel)
(source_file
  .
  [(function_definition) (macro_definition) (struct_definition) (abstract_definition)
   (primitive_definition) (module_definition)] @def.toplevel.first)
(module_definition
  (block
    [(function_definition) (macro_definition) (struct_definition) (abstract_definition)
     (primitive_definition)] @def.toplevel))
(module_definition
  (block
    .
    [(function_definition) (macro_definition) (struct_definition) (abstract_definition)
     (primitive_definition)] @def.toplevel.first))
