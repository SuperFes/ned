# Format captures (see Docs/FormattingRules.md for each name's pass). A
# module's procedures follow its `contains`.

(translation_unit
  [(program) (module) (submodule) (subroutine) (function) (block_data)] @def.toplevel)
(translation_unit
  .
  [(program) (module) (submodule) (subroutine) (function) (block_data)] @def.toplevel.first)

(internal_procedures [(subroutine) (function)] @def.method)
(internal_procedures (contains_statement) . [(subroutine) (function)] @def.method.first)
