#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). Pester's Describe/Context/It blocks.

((command
   command_name: (command_name) @_command
   command_elements: (command_elements
     .
     (command_argument_sep)
     .
     (array_literal_expression (unary_expression (string_literal) @test.name)))) @test.definition
 (:any-of? @_command "Describe" "Context" "It" "describe" "context" "it"))
