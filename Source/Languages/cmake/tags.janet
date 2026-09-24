#; Symbol-kind query. tree-sitter-cmake ships none. The first argument of
#; function()/macro() is the name.

(function_def
  (function_command
    (argument_list
      .
      (argument) @name))) @definition.function

(macro_def
  (macro_command
    (argument_list
      .
      (argument) @name))) @definition.function
