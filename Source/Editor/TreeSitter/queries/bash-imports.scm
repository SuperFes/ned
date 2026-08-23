; import-target-tree-sitter follow-up: checked against tree-sitter-bash's own
; node-types.json. "source"/"." are ordinary command names in this grammar
; (no dedicated node type), matched via a #any-of? predicate on the command's
; own name -- Query.h already evaluates this predicate.
(command
  name: (command_name (word) @_cmd)
  argument: (word) @import.target
  (#any-of? @_cmd "source" ".")) @import.statement
(command
  name: (command_name (word) @_cmd)
  argument: (string (string_content) @import.target)
  (#any-of? @_cmd "source" ".")) @import.statement
