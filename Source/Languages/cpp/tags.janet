# Ned's own vendor of tree-sitter-cpp's queries/tags.scm (gutter-symbol-kind
# follow-up) -- same "most vexing parse" fix as c-tags.scm's own header
# comment describes (see that file), applied to the @definition.function
# pattern only. The field_identifier and qualified_identifier @name patterns
# below (in-class method declarations and out-of-line `Class::method`
# definitions) are upstream's own, unmodified -- neither is vulnerable to the
# same ambiguity: a class member can't use constructor-call-style
# parenthesized init in a field declaration, and a qualified name
# (`Foo::bar`) is never valid as a local variable's own identifier. The
# reference_declarator-wrapped variant below covers a reference-returning
# function (`int& foo(...)`); pointer_declarator wrapping mirrors
# c-tags.scm's own pointer/pointer-to-pointer coverage.
#
# case-catalogue follow-up: everything below this point (struct split out of
# @definition.class, @definition.enum split out of @definition.type, the
# in-class-method captures renamed @definition.function -> @definition.method,
# and the entirely new field/macro/enum_member/template_parameter patterns)
# is ned's own addition -- checked directly against
# ThirdParty/tree-sitter-grammars/tree-sitter-cpp/src/node-types.json, not
# guessed, then confirmed live via Tests/ModeTest.cpp/Tests/FormatCaseTest.cpp
# parses of real snippets. Deliberately NOT covered: a const/constexpr-
# qualified "constant" bucket (distinguishing it from a plain field needs a
# #match?/#eq? predicate against an unnamed type_qualifier child -- real,
# just not attempted this pass), and a top-level "global" entity kind
# (`(translation_unit (declaration declarator: (identifier) @name)))
# @definition.var` -- tried, reverted: a source file with many top-level
# declarations -- Tests/BufferViewTest.cpp's own "Fold header glyphs still
# render after scrolling past an earlier foldable block" regression test
# uses 60 -- flooded the symbol-kind gutter's per-line marker stream and
# broke both that test and StickyScrollTest.cpp's viewport-exit case,
# neither of which this follow-up's own scope covers fixing. A named-
# namespace-scoped global was always going to be excluded anyway, for the
# same reason c-tags.scm's own header comment gives for pointer-returning
# functions: a curated subset, not exhaustive).

(struct_specifier name: (type_identifier) @name body:(_)) @definition.struct

(declaration type: (union_specifier name: (type_identifier) @name)) @definition.struct

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

# main-editor-sticky-scroll follow-up: the bare function_declarator-anchored
# patterns below (upstream's own, kept unmodified) are what a bodyless
# member declaration/prototype needs -- there's no enclosing function_definition
# for those at all, so this is the only range they can ever get. But the SAME
# pattern also matches an in-class inline method's or an out-of-line
# `Class::method` definition's own declarator, capturing only `run()`/
# `Widget::run()` rather than the whole body -- fine for the gutter (only
# ever reads startByte) but wrong for sticky scroll's containment check,
# which needs a method's range to actually extend through its body. The two
# function_definition-wrapped patterns just below cover that with-body case
# with the correct, wider range; Mode.cpp's symbolKind builder dedupes the
# resulting narrow/wide overlap for a with-body definition (same name+kind,
# one range nested in the other) down to the wider one.
#
# case-catalogue follow-up: both @definition.function -> @definition.method
# below (the field_identifier-declarator variants) -- a function_declarator
# whose OWN declarator is a field_identifier, rather than a plain identifier,
# only ever occurs inside a class/struct body (a free top-level function's
# name parses as a plain `identifier`, never a `field_identifier` -- checked
# against node-types.json). So these were always methods, in-class inline or
# bodyless prototype alike; the old @definition.function tag on them was
# itself the "conflates free functions with in-class methods" gap
# FormatCase.cpp's own comment used to call out, not a deliberate choice.
(function_definition
  declarator: (function_declarator
    declarator: (field_identifier) @name)) @definition.method

(function_definition
  declarator: (function_declarator
    declarator: (qualified_identifier scope: (namespace_identifier) @local.scope name: (identifier) @name))) @definition.method

(function_declarator declarator: (field_identifier) @name) @definition.method

(function_declarator declarator: (qualified_identifier scope: (namespace_identifier) @local.scope name: (identifier) @name)) @definition.method

(type_definition declarator: (type_identifier) @name) @definition.type

(enum_specifier name: (type_identifier) @name) @definition.enum

(class_specifier name: (type_identifier) @name) @definition.class

# main-editor-sticky-scroll follow-up: not part of upstream tree-sitter-cpp's
# own tags.scm -- added here (ned's own vendored file, see this file's own
# header comment) so a namespace shows up in the sticky-scroll breadcrumb.
# Anonymous namespaces (no `name:` field) simply don't match, which is
# correct: there's no name to show in a breadcrumb for one.
(namespace_definition name: (namespace_identifier) @name) @definition.namespace

# case-catalogue follow-up: a plain data member -- declarator is a bare (or
# pointer-to) field_identifier, never a function_declarator, so this can't
# overlap the method patterns above (`int x;`/`int* x;`, not `int f();`).
(field_declaration declarator: (field_identifier) @name) @definition.field

(field_declaration declarator: (pointer_declarator declarator: (field_identifier) @name)) @definition.field

# `#define NAME ...` and `#define NAME(...) ...` alike.
(preproc_def name: (identifier) @name) @definition.macro

(preproc_function_def name: (identifier) @name) @definition.macro

(enumerator name: (identifier) @name) @definition.enum_member

(type_parameter_declaration (type_identifier) @name) @definition.template_parameter
