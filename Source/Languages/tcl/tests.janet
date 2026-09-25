#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). tcltest's `test name description ...`.

((command
   name: (simple_word) @_command
   arguments: (word_list . (simple_word) @test.name)) @test.definition
 (:eq? @_command "test"))
