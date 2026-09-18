#; Symbol-kind query, ned-authored: TravonteD/tree-sitter-fennel ships no
#; queries. fn/lambda name themselves in `name:`; local/var/global bind
#; their first symbol.
(fn
  name: (symbol) @name) @definition.function
(fn
  name: (multi_symbol
    (symbol) @name .)) @definition.method
(lambda
  name: (symbol) @name) @definition.function
(local
  (binding
    (symbol) @name)) @definition.var
(var
  (binding
    (symbol) @name)) @definition.var
(global
  (binding
    (symbol) @name)) @definition.var
