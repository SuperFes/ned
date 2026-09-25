#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). Spock feature methods, named by their string, and
#; JUnit's @Test methods.

(function_definition
  function: (quoted_identifier) @test.name) @test.definition

((function_definition
   (annotation (identifier) @_annotation)
   function: (identifier) @test.name) @test.definition
 (:any-of? @_annotation "Test" "ParameterizedTest" "RepeatedTest"))
