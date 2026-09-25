#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). Crystal's spec: describe/context/it blocks named by
#; their first argument.

((call
   method: (identifier) @_block
   arguments: (argument_list . [(string) (constant)] @test.name)
   block: (block)) @test.definition
 (:any-of? @_block "describe" "context" "it" "pending"))
