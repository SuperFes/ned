#; Symbol-kind query, ned-authored (tree-sitter-http ships none). Each
#; request, named by its `###` separator's title, or by its URL when the
#; separator has none; plus the file's `@variables`.

(section
  (request_separator
    (value) @name)
  (request)) @definition.function

(section
  .
  (request
    (target_url) @name)) @definition.function

(section
  (request_separator
    !value)
  (request
    (target_url) @name)) @definition.function

(variable_declaration
  (identifier) @name) @definition.variable
