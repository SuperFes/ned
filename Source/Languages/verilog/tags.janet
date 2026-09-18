#; Symbol-kind query, ned-authored: tree-sitter/tree-sitter-verilog ships
#; no queries. Modules, functions and tasks are the outline.
(module_header
  (simple_identifier) @name) @definition.module
(function_body_declaration
  (function_identifier
    (function_identifier
      (simple_identifier) @name))) @definition.function
(task_body_declaration
  (task_identifier
    (task_identifier
      (simple_identifier) @name))) @definition.function
(class_declaration
  (class_identifier
    (simple_identifier) @name)) @definition.class
(package_declaration
  (package_identifier
    (simple_identifier) @name)) @definition.module
