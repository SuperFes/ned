# `(require :a.b)` names its module with a keyword string, `(require "a.b")`
# with a quoted one.
(list
  . (symbol) @_head
  . (string) @import.module
  (:any-of? @_head "require" "include")
  (:match? @import.module "^:")
  (:offset! @import.module 0 1 0 0)) @import.statement
(list
  . (symbol) @_head
  . (string) @import.module
  (:any-of? @_head "require" "include")
  (:match? @import.module "^\"")) @import.statement
(list
  . (symbol) @_head
  . (_)
  . (string) @import.module
  (:eq? @_head "import-macros")
  (:match? @import.module "^:")
  (:offset! @import.module 0 1 0 0)) @import.statement
(list
  . (symbol) @_head
  . (_)
  . (string) @import.module
  (:eq? @_head "import-macros")
  (:match? @import.module "^\"")) @import.statement
