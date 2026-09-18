#; Symbol-kind query, ned-authored: alaviss/tree-sitter-nim ships
#; highlights only. Every routine kind shares one hidden declaration shape
#; whose name is the first symbol; types and top-level bindings follow.
(proc_declaration
  name: (_) @name) @definition.function
(func_declaration
  name: (_) @name) @definition.function
(method_declaration
  name: (_) @name) @definition.method
(iterator_declaration
  name: (_) @name) @definition.function
(macro_declaration
  name: (_) @name) @definition.function
(template_declaration
  name: (_) @name) @definition.function
(converter_declaration
  name: (_) @name) @definition.function

(type_declaration
  (type_symbol_declaration
    name: (_) @name)) @definition.type

(const_section
  (variable_declaration
    (symbol_declaration_list
      (symbol_declaration
        name: (_) @name)))) @definition.constant
