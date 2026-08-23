; import-target-tree-sitter follow-up: #include target extraction, checked
; against tree-sitter-c's and tree-sitter-cpp's own node-types.json (both
; define preproc_include identically -- shared by CMode and CppMode, the
; same kTypeScript-style sharing Mode.h's own TsxMode doc comment already
; explains). preproc_include's "path" field can also be an identifier or
; call_expression (a macro-expanded include, e.g. "#include SOME_HEADER") --
; deliberately not matched, same scope this project's existing generic
; Link.cpp text scanner never covered either.
(preproc_include
  path: [(string_literal) (system_lib_string)] @import.target) @import.statement
