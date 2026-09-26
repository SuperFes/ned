# Module names are file names anywhere in the project (foo.ml is Foo).
(open_module (module_path) @import.module) @import.statement
(include_module (module_path) @import.module) @import.statement
(module_binding (module_name) . (module_path) @import.module)
