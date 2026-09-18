#; Symbol-kind query, ned-authored: tree-sitter-grammars/tree-sitter-objc
#; ships highlights, injections and locals but no tags. C's declarator
#; chain for functions; the bare identifier after @interface /
#; @implementation / @protocol (their headers are hidden rules) for
#; classes; a method's first selector word, likewise a bare identifier
#; child, for methods.
(function_definition
  declarator: (function_declarator
    declarator: (identifier) @name)) @definition.function
(function_definition
  declarator: (pointer_declarator
    declarator: (function_declarator
      declarator: (identifier) @name))) @definition.function

(class_interface
  (identifier) @name) @definition.class
(class_implementation
  (identifier) @name) @definition.class
(protocol_declaration
  (identifier) @name) @definition.interface

(method_definition
  (identifier) @name) @definition.method
(method_declaration
  (identifier) @name) @definition.method

(struct_specifier
  name: (type_identifier) @name
  body: (_)) @definition.class
(enum_specifier
  name: (type_identifier) @name
  body: (_)) @definition.enum
(type_definition
  declarator: (type_identifier) @name) @definition.type
