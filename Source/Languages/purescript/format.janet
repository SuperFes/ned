(purescript [(signature) (function) (data) (newtype) (type_alias) (class_declaration) (class_instance)
             (foreign_import)] @def.toplevel)

# A definition under its own signature, and each further equation of it.
(purescript (signature name: (_) @_sig) . (function name: (_) @_name) @def.toplevel.attached (:eq? @_sig @_name))
(purescript (function name: (_) @_prev) . (function name: (_) @_name) @def.toplevel.attached (:eq? @_prev @_name))
