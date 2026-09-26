# alias/import/require/use name a module, which Mix's layout files under
# lib/ in snake_case. The multi-alias form (`alias A.{B, C}`) is left out:
# its path is split across the dot and the tuple.
(call
  target: (identifier) @_directive
  (arguments . (alias) @import.module)
  (:any-of? @_directive "alias" "import" "require" "use")) @import.statement
