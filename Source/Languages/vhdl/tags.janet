#; Symbol-kind query, ned-authored: jpt13653903/tree-sitter-vhdl ships
#; highlights and injections (in its Neovim set) but no tags.
(entity_declaration
  entity: (identifier) @name) @definition.class
(architecture_definition
  architecture: (identifier) @name) @definition.module
(package_declaration
  package: (identifier) @name) @definition.module
(component_declaration
  component: (identifier) @name) @definition.class
(type_declaration
  type: (identifier) @name) @definition.type
(subprogram_declaration
  (function_specification
    function: (_) @name)) @definition.function
(subprogram_declaration
  (procedure_specification
    procedure: (_) @name)) @definition.function
(subprogram_definition
  (function_specification
    function: (_) @name)) @definition.function
(subprogram_definition
  (procedure_specification
    procedure: (_) @name)) @definition.function
(process_statement
  (label_declaration
    (label) @name)) @definition.function
(signal_declaration
  (identifier_list
    (identifier) @name)) @definition.var
