# change-signature follow-up: ned's own query, no upstream convention to
# vendor (same "ned-local capture convention" shape as tests.janet). Mirrors
# cpp/tags.janet's own @definition.function/@definition.method patterns
# exactly -- same node shapes, same function_definition-anchoring rule -- with
# one addition, a `parameters:` field capture, since that's the one thing
# tags.janet never needed and Editor/ChangeSignature.h does.
#
# The function_definition anchor on every plain-identifier pattern below is
# not a stylistic echo of tags.janet, it's load-bearing for the same reason:
# a bare `(function_declarator declarator: (identifier) ...)` pattern is
# ambiguous with C++'s "most vexing parse" (`Type name(args);` parses
# identically whether it's a real prototype or a local variable declared
# constructor-call-style -- tree-sitter has no symbol table to tell
# `std::ifstream file(path, mode);` from `int add(int a, int b);`
# syntactically). Anchoring on function_definition (only present with a real
# `{ ... }` body) resolves it, at the cost of never seeing a free function's
# own bodyless prototype -- change-signature reports that as "no prototype
# found" rather than risk rewriting a variable declaration.
#
# field_identifier/qualified_identifier (an in-class method or an out-of-line
# `Class::method`) carry no such ambiguity -- neither shape is valid local-
# variable syntax -- so those get the bare function_declarator pattern too,
# exactly as tags.janet's own bodyless-prototype coverage does; this is what
# lets a header's own `void method(...);` prototype be found at all.

(function_definition
  declarator: (function_declarator
    declarator: (identifier) @signature.name
    parameters: (parameter_list) @signature.parameters)) @signature.definition

(function_definition
  declarator: (pointer_declarator
    declarator: (function_declarator
      declarator: (identifier) @signature.name
      parameters: (parameter_list) @signature.parameters))) @signature.definition

(function_definition
  declarator: (pointer_declarator
    declarator: (pointer_declarator
      declarator: (function_declarator
        declarator: (identifier) @signature.name
        parameters: (parameter_list) @signature.parameters)))) @signature.definition

(function_definition
  declarator: (reference_declarator
    (function_declarator
      declarator: (identifier) @signature.name
      parameters: (parameter_list) @signature.parameters))) @signature.definition

(function_definition
  declarator: (function_declarator
    declarator: (field_identifier) @signature.name
    parameters: (parameter_list) @signature.parameters)) @signature.definition

(function_definition
  declarator: (pointer_declarator
    declarator: (function_declarator
      declarator: (field_identifier) @signature.name
      parameters: (parameter_list) @signature.parameters))) @signature.definition

(function_definition
  declarator: (reference_declarator
    (function_declarator
      declarator: (field_identifier) @signature.name
      parameters: (parameter_list) @signature.parameters))) @signature.definition

(function_definition
  declarator: (function_declarator
    declarator: (qualified_identifier name: (identifier) @signature.name)
    parameters: (parameter_list) @signature.parameters)) @signature.definition

(function_definition
  declarator: (pointer_declarator
    declarator: (function_declarator
      declarator: (qualified_identifier name: (identifier) @signature.name)
      parameters: (parameter_list) @signature.parameters))) @signature.definition

(function_definition
  declarator: (reference_declarator
    (function_declarator
      declarator: (qualified_identifier name: (identifier) @signature.name)
      parameters: (parameter_list) @signature.parameters))) @signature.definition

# Bodyless prototypes -- field_identifier/qualified_identifier only, per this
# file's own header comment on why a plain identifier never gets this bare
# (no function_definition) pattern.
(function_declarator
  declarator: (field_identifier) @signature.name
  parameters: (parameter_list) @signature.parameters) @signature.definition

(function_declarator
  declarator: (qualified_identifier name: (identifier) @signature.name)
  parameters: (parameter_list) @signature.parameters) @signature.definition
