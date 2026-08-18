; Vendored from nvim-treesitter/nvim-treesitter (generic-tree-sitter-highlighting
; follow-up) -- see c.scm's own header comment for the full story of why
; (this project's own currently-fetched tree-sitter-cpp query is similarly
; sparse, and nvim-treesitter's own current default branch defers to it
; rather than bundling its own).
;
;   Source: https://github.com/nvim-treesitter/nvim-treesitter
;   Paths:  queries/c/highlights.scm, queries/cpp/highlights.scm
;   Commit: cf12346a3414fa1b06af75c79faebe7f76df080a (master, fetched 2026-08-17)
;   License: Apache-2.0 (see that repo's own LICENSE)
;
; cpp/highlights.scm's own first line reads "; inherits: c" -- a real
; nvim-treesitter-specific query-loading convention (layer this file on top
; of c's own) that Query.cpp's own embedding mechanism has no equivalent
; concept for and doesn't need one just for two files: resolved here by
; literal inclusion at vendor time instead -- this file is genuinely both,
; concatenated, not cpp's own content alone. Same two custom additions as
; c.scm appended at the end (methods carry the same `type:` return-type
; field function_definition already has for free functions -- confirmed via
; `tree-sitter parse` against a real class with a method body).

; ============================================================
; c/highlights.scm (this grammar's own base -- see c.scm for the identical
; copy of this section, kept in sync by hand since there's no shared-
; fragment mechanism here worth building for just two files)
; ============================================================

; Lower priority to prefer @variable.parameter when identifier appears in parameter_declaration.
((identifier) @variable
  (#set! priority 95))

(preproc_def
  (preproc_arg) @variable)

[
  "default"
  "goto"
  "asm"
  "__asm__"
] @keyword

[
  "enum"
  "struct"
  "union"
  "typedef"
] @keyword.type

[
  "sizeof"
  "offsetof"
] @keyword.operator

(alignof_expression
  .
  _ @keyword.operator)

"return" @keyword.return

[
  "while"
  "for"
  "do"
  "continue"
  "break"
] @keyword.repeat

[
  "if"
  "else"
  "case"
  "switch"
] @keyword.conditional

[
  "#if"
  "#ifdef"
  "#ifndef"
  "#else"
  "#elif"
  "#endif"
  "#elifdef"
  "#elifndef"
  (preproc_directive)
] @keyword.directive

"#define" @keyword.directive.define

"#include" @keyword.import

[
  ";"
  ":"
  ","
  "."
  "::"
] @punctuation.delimiter

"..." @punctuation.special

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

[
  "="
  "-"
  "*"
  "/"
  "+"
  "%"
  "~"
  "|"
  "&"
  "^"
  "<<"
  ">>"
  "->"
  "<"
  "<="
  ">="
  ">"
  "=="
  "!="
  "!"
  "&&"
  "||"
  "-="
  "+="
  "*="
  "/="
  "%="
  "|="
  "&="
  "^="
  ">>="
  "<<="
  "--"
  "++"
] @operator

; Make sure the comma operator is given a highlight group after the comma
; punctuator so the operator is highlighted properly.
(comma_expression
  "," @operator)

[
  (true)
  (false)
] @boolean

(conditional_expression
  [
    "?"
    ":"
  ] @keyword.conditional.ternary)

(string_literal) @string

(system_lib_string) @string

(escape_sequence) @string.escape

(null) @constant.builtin

(number_literal) @number

(char_literal) @character

(preproc_defined) @function.macro

((field_expression
  (field_identifier) @property) @_parent
  (#not-has-parent? @_parent template_method function_declarator call_expression))

(field_designator) @property

((field_identifier) @property
  (#has-ancestor? @property field_declaration)
  (#not-has-ancestor? @property function_declarator))

(statement_identifier) @label

(declaration
  type: (type_identifier) @_type
  declarator: (identifier) @label
  (#eq? @_type "__label__"))

[
  (type_identifier)
  (type_descriptor)
] @type

(storage_class_specifier) @keyword.modifier

[
  (type_qualifier)
  (gnu_asm_qualifier)
  "__extension__"
] @keyword.modifier

(linkage_specification
  "extern" @keyword.modifier)

(type_definition
  declarator: (type_identifier) @type.definition)

(primitive_type) @type.builtin

(sized_type_specifier
  _ @type.builtin
  type: _?)

((identifier) @constant
  (#lua-match? @constant "^[A-Z][A-Z0-9_]+$"))

(preproc_def
  (preproc_arg) @constant
  (#lua-match? @constant "^[A-Z][A-Z0-9_]+$"))

(enumerator
  name: (identifier) @constant)

(case_statement
  value: (identifier) @constant)

((identifier) @constant.builtin
  ; format-ignore
  (#any-of? @constant.builtin
    "stderr" "stdin" "stdout"
    "__FILE__" "__LINE__" "__DATE__" "__TIME__"
    "__STDC__" "__STDC_VERSION__" "__STDC_HOSTED__"
    "__cplusplus" "__OBJC__" "__ASSEMBLER__"
    "__BASE_FILE__" "__FILE_NAME__" "__INCLUDE_LEVEL__"
    "__TIMESTAMP__" "__clang__" "__clang_major__"
    "__clang_minor__" "__clang_patchlevel__"
    "__clang_version__" "__clang_literal_encoding__"
    "__clang_wide_literal_encoding__"
    "__FUNCTION__" "__func__" "__PRETTY_FUNCTION__"
    "__VA_ARGS__" "__VA_OPT__"))

(preproc_def
  (preproc_arg) @constant.builtin
  ; format-ignore
  (#any-of? @constant.builtin
    "stderr" "stdin" "stdout"
    "__FILE__" "__LINE__" "__DATE__" "__TIME__"
    "__STDC__" "__STDC_VERSION__" "__STDC_HOSTED__"
    "__cplusplus" "__OBJC__" "__ASSEMBLER__"
    "__BASE_FILE__" "__FILE_NAME__" "__INCLUDE_LEVEL__"
    "__TIMESTAMP__" "__clang__" "__clang_major__"
    "__clang_minor__" "__clang_patchlevel__"
    "__clang_version__" "__clang_literal_encoding__"
    "__clang_wide_literal_encoding__"
    "__FUNCTION__" "__func__" "__PRETTY_FUNCTION__"
    "__VA_ARGS__" "__VA_OPT__"))

(attribute_specifier
  (argument_list
    (identifier) @variable.builtin))

(attribute_specifier
  (argument_list
    (call_expression
      function: (identifier) @variable.builtin)))

((call_expression
  function: (identifier) @function.builtin)
  (#lua-match? @function.builtin "^__builtin_"))

((call_expression
  function: (identifier) @function.builtin)
  (#has-ancestor? @function.builtin attribute_specifier))

; Preproc def / undef
(preproc_def
  name: (_) @constant.macro)

(preproc_call
  directive: (preproc_directive) @_u
  argument: (_) @constant.macro
  (#eq? @_u "#undef"))

(preproc_ifdef
  name: (identifier) @constant.macro)

(preproc_elifdef
  name: (identifier) @constant.macro)

(preproc_defined
  (identifier) @constant.macro)

(call_expression
  function: (identifier) @function.call)

(call_expression
  function: (field_expression
    field: (field_identifier) @function.call))

(function_declarator
  declarator: (identifier) @function)

(function_declarator
  declarator: (parenthesized_declarator
    (pointer_declarator
      declarator: (field_identifier) @function)))

(preproc_function_def
  name: (identifier) @function.macro)

(comment) @comment @spell

((comment) @comment.documentation
  (#lua-match? @comment.documentation "^/[*][*][^*].*[*]/$"))

; Parameters
(parameter_declaration
  declarator: (identifier) @variable.parameter)

(parameter_declaration
  declarator: (array_declarator) @variable.parameter)

(parameter_declaration
  declarator: (pointer_declarator) @variable.parameter)

(preproc_params
  (identifier) @variable.parameter)

[
  "__attribute__"
  "__declspec"
  "__based"
  "__cdecl"
  "__clrcall"
  "__stdcall"
  "__fastcall"
  "__thiscall"
  "__vectorcall"
  (ms_pointer_modifier)
  (attribute_declaration)
] @attribute

; ============================================================
; cpp/highlights.scm (this grammar's own additions on top of c's)
; ============================================================

((identifier) @variable.member
  (#lua-match? @variable.member "^m_.*$"))

(parameter_declaration
  declarator: (reference_declarator) @variable.parameter)

; function(Foo ...foo)
(variadic_parameter_declaration
  declarator: (variadic_declarator
    (_) @variable.parameter))

; int foo = 0
(optional_parameter_declaration
  declarator: (_) @variable.parameter)

;(field_expression) @variable.parameter ;; How to highlight this?
((field_expression
  (field_identifier) @function.method) @_parent
  (#has-parent? @_parent template_method function_declarator))

(field_declaration
  (field_identifier) @variable.member)

(field_initializer
  (field_identifier) @property)

(function_declarator
  declarator: (field_identifier) @function.method)

(concept_definition
  name: (identifier) @type.definition)

(alias_declaration
  name: (type_identifier) @type.definition)

(auto) @type.builtin

(namespace_identifier) @module

((namespace_identifier) @type
  (#lua-match? @type "^[%u]"))

(case_statement
  value: (qualified_identifier
    (identifier) @constant))

(using_declaration
  .
  "using"
  .
  "namespace"
  .
  [
    (qualified_identifier)
    (identifier)
  ] @module)

(destructor_name
  (identifier) @function.method)

; functions
(function_declarator
  (qualified_identifier
    (identifier) @function))

(function_declarator
  (qualified_identifier
    (qualified_identifier
      (identifier) @function)))

(function_declarator
  (qualified_identifier
    (qualified_identifier
      (qualified_identifier
        (identifier) @function))))

((qualified_identifier
  (qualified_identifier
    (qualified_identifier
      (qualified_identifier
        (identifier) @function)))) @_parent
  (#has-ancestor? @_parent function_declarator))

(function_declarator
  (template_function
    (identifier) @function))

(operator_name) @function

"operator" @function

"static_assert" @function.builtin

(call_expression
  (qualified_identifier
    (identifier) @function.call))

(call_expression
  (qualified_identifier
    (qualified_identifier
      (identifier) @function.call)))

(call_expression
  (qualified_identifier
    (qualified_identifier
      (qualified_identifier
        (identifier) @function.call))))

((qualified_identifier
  (qualified_identifier
    (qualified_identifier
      (qualified_identifier
        (identifier) @function.call)))) @_parent
  (#has-ancestor? @_parent call_expression))

(call_expression
  (template_function
    (identifier) @function.call))

(call_expression
  (qualified_identifier
    (template_function
      (identifier) @function.call)))

(call_expression
  (qualified_identifier
    (qualified_identifier
      (template_function
        (identifier) @function.call))))

(call_expression
  (qualified_identifier
    (qualified_identifier
      (qualified_identifier
        (template_function
          (identifier) @function.call)))))

((qualified_identifier
  (qualified_identifier
    (qualified_identifier
      (qualified_identifier
        (template_function
          (identifier) @function.call))))) @_parent
  (#has-ancestor? @_parent call_expression))

; methods
(function_declarator
  (template_method
    (field_identifier) @function.method))

(call_expression
  (field_expression
    (field_identifier) @function.method.call))

; constructors
((function_declarator
  (qualified_identifier
    (identifier) @constructor))
  (#lua-match? @constructor "^%u"))

((call_expression
  function: (identifier) @constructor)
  (#lua-match? @constructor "^%u"))

((call_expression
  function: (qualified_identifier
    name: (identifier) @constructor))
  (#lua-match? @constructor "^%u"))

((call_expression
  function: (field_expression
    field: (field_identifier) @constructor))
  (#lua-match? @constructor "^%u"))

; constructing a type in an initializer list: Constructor ():  **SuperType (1)**
((field_initializer
  (field_identifier) @constructor
  (argument_list))
  (#lua-match? @constructor "^%u"))

; Constants
(this) @variable.builtin

(null
  "nullptr" @constant.builtin)

(true) @boolean

(false) @boolean

; Literals
(raw_string_literal) @string

; Keywords
[
  "try"
  "catch"
  "noexcept"
  "throw"
] @keyword.exception

[
  "decltype"
  "explicit"
  "friend"
  "override"
  "using"
  "requires"
  "constexpr"
] @keyword

[
  "class"
  "namespace"
  "template"
  "typename"
  "concept"
] @keyword.type

[
  "co_await"
  "co_yield"
  "co_return"
] @keyword.coroutine

[
  "public"
  "private"
  "protected"
  "final"
  "virtual"
] @keyword.modifier

[
  "new"
  "delete"
  "xor"
  "bitand"
  "bitor"
  "compl"
  "not"
  "xor_eq"
  "and_eq"
  "or_eq"
  "not_eq"
  "and"
  "or"
] @keyword.operator

"<=>" @operator

"::" @punctuation.delimiter

(template_argument_list
  [
    "<"
    ">"
  ] @punctuation.bracket)

(template_parameter_list
  [
    "<"
    ">"
  ] @punctuation.bracket)

(literal_suffix) @operator

; ---------------------------------------------------------------------------
; Ned's own additions (generic-tree-sitter-highlighting follow-up) -- real
; changes from the vendored original above, not upstream nvim-treesitter
; content. Both structural only, no predicates -- placed last so their
; "later capture wins" (see Mode.h's own HighlightSpan doc comment) takes
; priority over the broader patterns above for the same node. Node/field
; names confirmed against the real bundled grammar via `tree-sitter parse`,
; not guessed -- including that a method's own function_definition carries
; the same `type:` field a free function's does.

; Splits "#include <header>" from "#include \"header\"" -- both matched the
; same plain @string above; system_lib_string only ever appears as an
; #include path in this grammar, so no extra field constraint is needed.
(system_lib_string) @string.special.include

; A function's or method's own return type, distinct from every other
; @type/@type.builtin usage (a parameter's type, a member variable's type,
; a cast, ...). Definitions (function_definition) always carry their own
; `type:` field regardless of how deeply the declarator is pointer-wrapped.
; Prototypes (plain `declaration`) need an explicit declarator-shape check
; to avoid also tagging a plain variable's type ("int x;") as a return type
; -- covers the common unwrapped and single-pointer-wrapped cases; a
; multiply-pointer-wrapped prototype ("char** foo();") falls back to the
; generic @type, a known, narrow limitation, not silently wrong.
(function_definition type: (_) @type.return)
(declaration
  type: (_) @type.return
  declarator: [
    (function_declarator)
    (pointer_declarator (function_declarator))
  ])
