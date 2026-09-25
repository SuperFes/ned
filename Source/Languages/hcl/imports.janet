# A Terraform module's local source directory.
(block
  (identifier) @_block
  (body
    (attribute
      (identifier) @_attribute
      (expression (literal_value (string_lit (template_literal) @import.target)))))
  (:eq? @_block "module")
  (:eq? @_attribute "source")) @import.statement
