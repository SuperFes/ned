; Vendored from nvim-treesitter/nvim-treesitter (generic-tree-sitter-highlighting
; follow-up) -- its own repo doesn't bundle any highlight queries on its
; current default branch anymore (defers to each grammar's own, which is
; genuinely sparse: see the fetched build/_deps/tree-sitter-c-src/queries/
; highlights.scm for comparison -- ~20 capture names, no access-specifier/
; return-type distinction, both #include forms mapped to the same @string).
; The real, actively-maintained, ~40-category query still lives on
; nvim-treesitter's own `master` branch:
;
;   Source: https://github.com/nvim-treesitter/nvim-treesitter
;   Path:   queries/c/highlights.scm
;   Commit: cf12346a3414fa1b06af75c79faebe7f76df080a (master, fetched 2026-08-17)
;   License: Apache-2.0 (see that repo's own LICENSE)
;
; Vendored verbatim below (not fetched at build time -- this project's own
; established preference for inspectable/editable checked-in files over
; silent generation, the same call OrgHighlights.scm already made), with
; two small custom patterns of Ned's own appended at the end: real changes
; from the original, called out below per Apache-2.0's own "state changes
; made" term, not folded in silently. Reachable through
; Source/Editor/TreeSitter/Query.cpp's own predicate evaluation
; (generic-tree-sitter-highlighting follow-up) -- most of what's below is
; plain structural pattern matching with no predicate dependency at all,
; but the pieces that do use one (#lua-match?/#any-of?/#has-ancestor?/
; #eq?/#set!) are all real, now-supported predicates, not stripped out or
; adapted; see Query.h's own header comment for exactly what's evaluated
; and what's deliberately left inert (only #set!, or a genuinely
; unrecognized predicate name).

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

; ---------------------------------------------------------------------------
; Ned's own additions (generic-tree-sitter-highlighting follow-up) -- real
; changes from the vendored original above, not upstream nvim-treesitter
; content. Both structural only, no predicates -- placed last so their
; "later capture wins" (see Mode.h's own HighlightSpan doc comment) takes
; priority over the broader patterns above for the same node. Node/field
; names confirmed against the real bundled grammar via `tree-sitter parse`,
; not guessed.

; Splits "#include <header>" from "#include \"header\"" -- both matched the
; same plain @string above; system_lib_string only ever appears as an
; #include path in this grammar, so no extra field constraint is needed.
(system_lib_string) @string.special.include

; A function's own return type, distinct from every other @type/@type.builtin
; usage (a parameter's type, a variable's type, a cast, ...). Definitions
; (function_definition) always carry their own `type:` field regardless of
; how deeply the declarator is pointer-wrapped (confirmed: `char* foo() {...}`
; still has `type: (primitive_type)` directly on function_definition).
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
