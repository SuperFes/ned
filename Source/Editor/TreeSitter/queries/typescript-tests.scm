;; Verbatim copy of javascript-tests.scm -- see that file's header comment
;; (same node names in both grammars, same duplication the two
;; *-imports.scm files already accept).

((call_expression
   function: (identifier) @_fn
   arguments: (arguments . (string) @test.name)
   (#any-of? @_fn "it" "test" "describe")) @test.definition)

((call_expression
   function: (member_expression object: (identifier) @_fn)
   arguments: (arguments . (string) @test.name)
   (#any-of? @_fn "it" "test" "describe")) @test.definition)
