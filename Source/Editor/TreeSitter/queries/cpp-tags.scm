; Ned's own vendor of tree-sitter-cpp's queries/tags.scm (gutter-symbol-kind
; follow-up) -- same "most vexing parse" fix as c-tags.scm's own header
; comment describes (see that file), applied to the @definition.function
; pattern only. The field_identifier and qualified_identifier @name patterns
; below (in-class method declarations and out-of-line `Class::method`
; definitions) are upstream's own, unmodified -- neither is vulnerable to the
; same ambiguity: a class member can't use constructor-call-style
; parenthesized init in a field declaration, and a qualified name
; (`Foo::bar`) is never valid as a local variable's own identifier. The
; reference_declarator-wrapped variant below covers a reference-returning
; function (`int& foo(...)`); pointer_declarator wrapping mirrors
; c-tags.scm's own pointer/pointer-to-pointer coverage.

(struct_specifier name: (type_identifier) @name body:(_)) @definition.class

(declaration type: (union_specifier name: (type_identifier) @name)) @definition.class

(function_definition
  declarator: (function_declarator
    declarator: (identifier) @name)) @definition.function

(function_definition
  declarator: (pointer_declarator
    declarator: (function_declarator
      declarator: (identifier) @name))) @definition.function

(function_definition
  declarator: (pointer_declarator
    declarator: (pointer_declarator
      declarator: (function_declarator
        declarator: (identifier) @name)))) @definition.function

(function_definition
  declarator: (reference_declarator
    (function_declarator
      declarator: (identifier) @name))) @definition.function

(function_declarator declarator: (field_identifier) @name) @definition.function

(function_declarator declarator: (qualified_identifier scope: (namespace_identifier) @local.scope name: (identifier) @name)) @definition.method

(type_definition declarator: (type_identifier) @name) @definition.type

(enum_specifier name: (type_identifier) @name) @definition.type

(class_specifier name: (type_identifier) @name) @definition.class
