(file [(type_annotation) (value_declaration) (type_declaration) (type_alias_declaration) (port_annotation)
       (infix_declaration)] @def.toplevel)

# A value under its own type annotation.
(file (type_annotation name: (_) @_sig) . (value_declaration (function_declaration_left . (lower_case_identifier) @_name))
  @def.toplevel.attached (:eq? @_sig @_name))
