#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; nutest and the standard library's runner mark a test command with a
#; `#[test]` comment on the line above its `def`.

((_
   (comment) @_attribute
   .
   (decl_def [(val_string (string_content) @test.name) (cmd_identifier) @test.name]) @test.definition)
 (:match? @_attribute "^#\\[test\\]"))
