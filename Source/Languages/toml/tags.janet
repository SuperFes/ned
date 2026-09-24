#; Symbol-kind query, ned-authored (tree-sitter-toml ships none). Tables are
#; the outline; a key is listed only before the first table, where nothing
#; else would name it. A key inside a table is left out, or the outline
#; would be the file again.

(table
  .
  [(bare_key) (dotted_key) (quoted_key)] @name) @definition.namespace

(table_array_element
  .
  [(bare_key) (dotted_key) (quoted_key)] @name) @definition.namespace

(document
  (pair
    .
    [(bare_key) (dotted_key) (quoted_key)] @name) @definition.field)
