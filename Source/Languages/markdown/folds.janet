# ned-authored. Markdown folds by what it contains, not by delimiters (the
# language has `:imprint false`). A section folds under its heading -- the
# markdown.sections escape adds those, from the same extents the outline
# uses. A list item folds under its first line; the list itself does not,
# since it would open on that same row.
[(fenced_code_block) (list_item) (block_quote) (pipe_table) (html_block)] @fold
