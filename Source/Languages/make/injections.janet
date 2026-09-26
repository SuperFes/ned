# Each recipe line runs in its own shell. Make expands `$(VAR)` before the
# shell sees the line, so those read as command substitutions here.
((recipe_line) @injection.content
  (:set! injection.language "bash"))
