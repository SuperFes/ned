#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; fishtape's `@test "name" <actual> <operator> <expected>`.

((command
   .
   (word) @_cmd
   .
   [(double_quote_string) (single_quote_string) (word)] @test.name
   (:eq? @_cmd "@test")) @test.definition)
