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

; main-editor-sticky-scroll follow-up: the bare function_declarator-anchored
; patterns below (upstream's own, kept unmodified) are what a bodyless
; member declaration/prototype needs -- there's no enclosing function_definition
; for those at all, so this is the only range they can ever get. But the SAME
; pattern also matches an in-class inline method's or an out-of-line
; `Class::method` definition's own declarator, capturing only `run()`/
; `Widget::run()` rather than the whole body -- fine for the gutter (only
; ever reads startByte) but wrong for sticky scroll's containment check,
; which needs a method's range to actually extend through its body. The two
; function_definition-wrapped patterns just below cover that with-body case
; with the correct, wider range; Mode.cpp's symbolKind builder dedupes the
; resulting narrow/wide overlap for a with-body definition (same name+kind,
; one range nested in the other) down to the wider one.
(function_definition
  declarator: (function_declarator
    declarator: (field_identifier) @name)) @definition.function

(function_definition
  declarator: (function_declarator
    declarator: (qualified_identifier scope: (namespace_identifier) @local.scope name: (identifier) @name))) @definition.method

(function_declarator declarator: (field_identifier) @name) @definition.function

(function_declarator declarator: (qualified_identifier scope: (namespace_identifier) @local.scope name: (identifier) @name)) @definition.method

(type_definition declarator: (type_identifier) @name) @definition.type

(enum_specifier name: (type_identifier) @name) @definition.type

(class_specifier name: (type_identifier) @name) @definition.class

; main-editor-sticky-scroll follow-up: not part of upstream tree-sitter-cpp's
; own tags.scm -- added here (ned's own vendored file, see this file's own
; header comment) so a namespace shows up in the sticky-scroll breadcrumb.
; Anonymous namespaces (no `name:` field) simply don't match, which is
; correct: there's no name to show in a breadcrumb for one.
(namespace_definition name: (namespace_identifier) @name) @definition.namespace
