#; Symbol-kind query, ned-authored: tree-sitter-perl ships highlights,
#; injections, folds and textobjects but no tags.
(package_statement
  name: (package) @name) @definition.module
(class_statement
  name: (package) @name) @definition.class
(subroutine_declaration_statement
  name: (bareword) @name) @definition.function
(method_declaration_statement
  name: (bareword) @name) @definition.method
