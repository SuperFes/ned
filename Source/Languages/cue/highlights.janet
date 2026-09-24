# Upstream lists this after its `(identifier) @variable` catch-all, which a
# first-pattern-wins query never reaches; read ahead of upstream's file.
((identifier) @type
  (:match? @type "^(#|_#)"))
