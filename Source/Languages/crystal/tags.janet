#; Symbol-kind query, ned-authored: crystal-lang-tools/tree-sitter-crystal
#; ships highlights and injections but no tags. The shape is Ruby's.
(module_def
  name: (_) @name) @definition.module
(class_def
  name: (_) @name) @definition.class
(struct_def
  name: (_) @name) @definition.class
(enum_def
  name: (_) @name) @definition.enum
(lib_def
  name: (_) @name) @definition.module
(annotation_def
  name: (_) @name) @definition.class
(method_def
  name: (_) @name) @definition.method
(abstract_method_def
  name: (_) @name) @definition.method
(macro_def
  name: (_) @name) @definition.function
(fun_def
  name: (_) @name) @definition.function
(const_assign
  lhs: (constant) @name) @definition.constant
