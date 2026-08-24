; Ned's own vendor of tree-sitter-c's queries/tags.scm (gutter-symbol-kind
; follow-up). Everything below is upstream's own, unmodified, except the
; @definition.function pattern: upstream's own version is
; `(function_declarator declarator: (identifier) @name) @definition.function`,
; which matches *any* function_declarator regardless of context -- ambiguous
; with C's "most vexing parse": `Type name(args);` parses identically whether
; it's a real function prototype/definition or a local variable declared with
; constructor-call-style syntax (tree-sitter has no symbol table, so it can't
; tell `std::ifstream file(path, mode);` from `int add(int a, int b);`
; syntactically -- confirmed live against a real false positive, see
; BufferViewSymbolGutterTest.cpp). Anchoring on function_definition (which
; only exists when there's a real `{ ... }` body, per this grammar's own
; node-types.json -- a variable declaration never has one) resolves the
; ambiguity. This also means a bare prototype no longer gets the glyph either
; -- a deliberate tradeoff, arguably more correct anyway (a prototype isn't
; "the definition"). The two pointer_declarator-wrapped variants below cover
; a pointer- or pointer-to-pointer-returning function (`int* foo(...)`,
; `char** foo(...)`) -- deeper wrapping is left uncovered, the same "curated
; subset, not exhaustive" tradeoff this codebase already makes elsewhere.

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

(type_definition declarator: (type_identifier) @name) @definition.type

(enum_specifier name: (type_identifier) @name) @definition.type
