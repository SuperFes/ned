# Format captures (see Docs/FormattingRules.md for each name's pass). Nim's
# routines are all top level; an object's methods are procs beside it.

(source_file
  [(proc_declaration) (func_declaration) (method_declaration) (iterator_declaration)
   (template_declaration) (macro_declaration) (converter_declaration) (type_section)] @def.toplevel)
(source_file
  .
  [(proc_declaration) (func_declaration) (method_declaration) (iterator_declaration)
   (template_declaration) (macro_declaration) (converter_declaration) (type_section)] @def.toplevel.first)
