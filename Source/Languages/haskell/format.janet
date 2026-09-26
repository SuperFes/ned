(declarations [(signature) (function) (bind) (data_type) (newtype) (type_synomym) (class) (instance)
               (type_family) (deriving_instance)] @def.toplevel)
(declarations . [(signature) (function) (bind) (data_type) (newtype) (type_synomym) (class) (instance)
                 (type_family) (deriving_instance)] @def.toplevel.first)

# A definition under its own signature, and each further equation of it.
(declarations (signature name: (_) @_sig) . [(function name: (_) @_name) (bind name: (_) @_name)] @def.toplevel.attached
  (:eq? @_sig @_name))
(declarations (function name: (_) @_prev) . (function name: (_) @_name) @def.toplevel.attached
  (:eq? @_prev @_name))
