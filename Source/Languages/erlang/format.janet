(source_file [(fun_decl) (spec) (record_decl) (type_alias) (opaque) (callback)] @def.toplevel)
(source_file . [(fun_decl) (spec) (record_decl) (type_alias) (opaque) (callback)] @def.toplevel.first)

# A function under its own -spec, and each further clause of it.
(source_file (spec) . (fun_decl) @def.toplevel.attached)
(source_file (fun_decl clause: (function_clause name: (_) @_prev)) . (fun_decl clause: (function_clause name: (_) @_name))
  @def.toplevel.attached (:eq? @_prev @_name))
