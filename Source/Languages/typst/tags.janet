#; Symbol-kind query, ned-authored: headings are the outline, let
#; bindings the names.
(heading
  (text) @name) @definition.module
(let
  pattern: (ident) @name) @definition.var
(let
  pattern: (call
    item: (ident) @name)) @definition.function
