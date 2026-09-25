# A suite is one `body`/`class_body` node, opened by a zero-width scanner
# `_indent` on the first indented line and closed by `_dedent`. The imprint
# only infers Python's shape (the `_indent` owned by the parent rule), so the
# bodies are named here instead.
[(body) (class_body) (match_body)] @indent
