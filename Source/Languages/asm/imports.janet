# GNU as's .include/.incbin, NASM's %include and incbin.
(meta
  kind: (meta_ident) @_kind
  (string) @import.target
  (:any-of? @_kind ".include" ".incbin" "%include")) @import.statement
(instruction
  kind: (word) @_kind
  (string) @import.target
  (:eq? @_kind "incbin")) @import.statement
