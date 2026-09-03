; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention -- this is C's own indent query plus C++-only container
; node types: declaration_list covers namespace bodies (C++ doesn't reuse
; field_declaration_list for those the way it does for class/struct/union
; bodies). parameter_list/argument_list get "aligned" rather than "indent",
; same reasoning as c-indents.scm's own (checked directly against
; tree-sitter-cpp -- this file was a divergent copy of that query, not a
; deliberate C++-specific choice; the alignment behavior is unconditionally
; wrong here before this fix, confirmed by IndentTest.cpp's own CMode/
; JavaScriptMode wrapped-call-alignment tests having no CppMode counterpart).
(compound_statement) @indent
(field_declaration_list) @indent
(initializer_list) @indent
(parameter_list) @aligned
(argument_list) @aligned

; declaration_list is namespace_definition's own body (also
; linkage_specification's, i.e. `extern "C" { ... }` -- deliberately never
; captured at all, see below) -- but this project's own .clang-format sets
; NamespaceIndentation: Inner, meaning only a namespace GENUINELY NESTED
; inside another namespace indents its body; an ordinary top-level
; `namespace foo { ... }` (or `namespace {` / C++17's `namespace a::b::c {`,
; which tree-sitter-cpp parses as ONE flat namespace_definition, not nested
; ones -- confirmed via a real parse dump) does not, and this codebase's own
; on-disk formatting agrees (see e.g. main.cpp's own top-level `namespace {`
; wrapping RunInteractiveEditor -- its body sits at column 0, not indented).
; Capturing declaration_list unconditionally (the pre-fix version of this
; query) counted that non-indenting namespace as a real indent level anyway,
; over-indenting every statement typed inside it by one level -- a real,
; live-reported bug (a newline after an ordinary statement landing 2 levels
; deep instead of 1). @_ns anchors the OUTER namespace_definition owning
; this declaration_list purely so #has-ancestor? can check ITS OWN ancestry
; (does IT sit inside another namespace_definition), not the immediate,
; always-true parent relationship declaration_list itself has to its own
; owning namespace_definition. linkage_specification's declaration_list
; never matches this pattern at all (its parent isn't namespace_definition),
; which is exactly the desired "extern \"C\" blocks never indent" outcome --
; confirmed against this codebase's own Editor/TreeSitter/Languages.cpp.
((namespace_definition (declaration_list) @indent) @_ns
 (#has-ancestor? @_ns namespace_definition))

(compound_statement "}" @dedent)
(field_declaration_list "}" @dedent)
(declaration_list "}" @dedent)
(initializer_list "}" @dedent)
(parameter_list ")" @dedent)
(argument_list ")" @dedent)
