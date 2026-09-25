#; Symbol-kind query, ned-authored (tree-sitter-hcl ships none). Top-level
#; blocks are the outline, named by their last label (a Terraform resource
#; "aws_instance" "web" is "web"), or by their type when unlabelled; a
#; nested block is left to the breadcrumbs.

(config_file
  (body
    (block
      .
      (identifier) @name
      .
      (block_start)) @definition.namespace))

(config_file
  (body
    (block
      .
      (identifier) @_type
      (string_lit (template_literal) @name)
      .
      (block_start)
      (:any-of? @_type "variable" "output")) @definition.variable))

(config_file
  (body
    (block
      .
      (identifier) @_type
      (string_lit (template_literal) @name)
      .
      (block_start)
      (:eq? @_type "module")) @definition.module))

(config_file
  (body
    (block
      .
      (identifier) @_type
      (string_lit (template_literal) @name)
      .
      (block_start)
      (:not-any-of? @_type "variable" "output" "module")) @definition.class))

#; A Terraform local is referenced as local.<name>, so each is a symbol.
(config_file
  (body
    (block
      .
      (identifier) @_type
      .
      (block_start)
      (body
        (attribute
          .
          (identifier) @name) @definition.variable)
      (:eq? @_type "locals"))))

(config_file
  (body
    (attribute
      .
      (identifier) @name) @definition.field))
