#; Symbol-kind query, ned-authored (tree-sitter-diff ships none). Each file a
#; patch touches, then its hunks, named by their `@@` line.

(block
  (new_file
    (filename) @name)) @definition.module

(hunk
  (location) @name) @definition.function
