#; Symbol-kind query. Beaglefoot/tree-sitter-awk ships highlights only. A
#; user-defined function is the only thing an awk script names.

(func_def
  name: (_) @name) @definition.function
