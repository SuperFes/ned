# Ned's own addition beside the vendored upstream query (upstream/
# highlights.janet): the grammar has a real "strikethrough" node (GFM
# extension) but upstream does not capture it, and this is the only place
# that runs this grammar's highlighting -- it is injected into markdown, never
# a file type of its own.
(strikethrough) @text.strikethrough
