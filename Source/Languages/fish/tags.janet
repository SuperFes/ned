#; Symbol-kind query. tree-sitter-fish ships none. `set` is an ordinary
#; command whose name argument follows any number of flags, so only functions
#; are named.

(function_definition
  name: (_) @name) @definition.function
