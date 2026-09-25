(command
  name: (simple_word) @_command
  arguments: (word_list . (simple_word) @import.target)
  (:eq? @_command "source")) @import.statement
