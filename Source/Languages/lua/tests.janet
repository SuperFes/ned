#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). busted's describe/it blocks, and LuaUnit's test
#; functions and Test* table methods.

((function_call
   name: (identifier) @_fn
   arguments: (arguments . (string) @test.name (function_definition))) @test.definition
 (:any-of? @_fn "describe" "context" "insulate" "expose" "it" "spec" "test" "pending"))

((function_declaration
   name: (method_index_expression table: (identifier) @_table method: (identifier) @test.name)) @test.definition
 (:match? @_table "^[Tt]est"))

((function_declaration
   name: (identifier) @test.name) @test.definition
 (:match? @test.name "^[Tt]est"))
