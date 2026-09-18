#; Symbol-kind query, ned-authored: Isopod/tree-sitter-pascal ships
#; highlights and locals but no tags. A unit or program is the module; a
#; routine's header names it; types, constants, fields and properties are
#; the rest.
(program
  (moduleName) @name) @definition.module
(unit
  (moduleName) @name) @definition.module
(defProc
  header: (declProc
    name: (_) @name)) @definition.function
(declProc
  name: (_) @name) @definition.function
(declType
  name: (_) @name) @definition.type
(declConst
  name: (identifier) @name) @definition.constant
(declVar
  name: (identifier) @name) @definition.var
(declField
  name: (identifier) @name) @definition.field
(declProp
  name: (identifier) @name) @definition.property
