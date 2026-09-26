# Module names are file names anywhere in the project (Foo.res is Foo).
(open_statement [(module_identifier) (module_identifier_path)] @import.module) @import.statement
(include_statement [(module_identifier) (module_identifier_path)] @import.module) @import.statement
(module_binding (module_identifier) . [(module_identifier) (module_identifier_path)] @import.module)
