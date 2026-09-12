#; Test-discovery query (ned-local "@test.definition"/"@test.name" capture
#; convention -- see Mode::testDiscovery in Editor/Mode.h and cpp-tests.scm's
#; own header comment). Go's testing convention (checked against
#; tree-sitter/tree-sitter-go's own node-types.json, same discipline every
#; other *-tests.scm in this project holds to): a top-level
#; func TestXxx(t *testing.T) / BenchmarkXxx(b *testing.B) /
#; FuzzXxx(f *testing.F), matched by requiring its sole parameter's type be
#; a pointer to testing.T/B/F specifically -- a bare name-prefix match alone
#; would also catch an ordinary helper function someone happens to name
#; "TestHelperSetup" that takes no testing.* parameter at all.
#; ExampleXxx is its own, deliberately separate pattern: real Go Example
#; functions take no parameters at all (the language spec requires it), so
#; there is no testing.* type to anchor on.
(
  (function_declaration
    name: (identifier) @test.name
    parameters: (parameter_list
      (parameter_declaration
        type: (pointer_type
          (qualified_type
            package: (package_identifier) @_pkg
            name: (type_identifier) @_type))))
    (:eq? @_pkg "testing")
    (:any-of? @_type "T" "B" "F")
    (:match? @test.name "^(Test|Benchmark|Fuzz)")) @test.definition
)

(
  (function_declaration
    name: (identifier) @test.name
    parameters: (parameter_list)
    (:match? @test.name "^Example")) @test.definition
)
