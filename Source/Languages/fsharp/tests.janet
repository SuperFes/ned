#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). Expecto's testList/testCase family, and xUnit/NUnit/
#; MSTest attributed functions, whose double-backtick names lose their
#; backticks.

((application_expression
   (long_identifier_or_op (identifier) @_fn)
   (const (string) @test.name)) @test.definition
 (:any-of? @_fn "testList" "testCase" "testCaseAsync" "testProperty" "ptestCase" "ftestCase" "testSequenced"))

((declaration_expression
   (attributes (attribute (simple_type (long_identifier (identifier) @_attribute))))
   (function_or_value_defn (function_declaration_left . (identifier) @test.name))) @test.definition
 (:any-of? @_attribute "Fact" "Theory" "Test" "TestCase" "TestMethod" "Property")
 (:match? @test.name "^``")
 (:offset! @test.name 0 2 0 -2))

((declaration_expression
   (attributes (attribute (simple_type (long_identifier (identifier) @_attribute))))
   (function_or_value_defn (function_declaration_left . (identifier) @test.name))) @test.definition
 (:any-of? @_attribute "Fact" "Theory" "Test" "TestCase" "TestMethod" "Property")
 (:not-match? @test.name "^``"))
