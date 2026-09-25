(command
  name: (word) @_command
  argument: [(word) (double_quote_string) (single_quote_string)] @import.target
  (:any-of? @_command "source" ".")) @import.statement
