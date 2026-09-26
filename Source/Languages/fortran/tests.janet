#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; test-drive's `new_unittest("name", procedure)` (fpm's own framework) and
#; veggies' `describe`/`it`, which register a test by the name they are
#; given; pFUnit's `@test` directive above a module procedure.

((call_expression
   (identifier) @_fn
   (argument_list . (string_literal) @test.name)
   (:any-of? @_fn "new_unittest" "new_testsuite" "describe" "it")) @test.definition)

((_
   (custom_directive) @_directive
   .
   (subroutine (subroutine_statement (name) @test.name)) @test.definition)
 (:match? @_directive "^@[Tt][Ee][Ss][Tt]"))
