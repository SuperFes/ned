# ned-authored. Markdown folds by what it contains, not by delimiters (the
# language has `:imprint false`), so this query is its only fold source. A
# section folds under its heading -- the grammar's first section wraps the
# document even with no heading, so only one that opens with a heading
# counts. A list item folds under its first line; the list itself does not,
# since it would open on that same row.
((section . [(atx_heading) (setext_heading)]) @fold)
[(fenced_code_block) (list_item) (block_quote) (pipe_table) (html_block)] @fold
