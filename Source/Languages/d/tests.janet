#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; A `unittest` block named by a string UDA -- `@("name")`, or
#; unit-threaded's `@Name("name")` -- which unit-threaded and silly run
#; by that name. A bare `unittest` has no name to run by.

((unittest_declaration
   (at_attribute (expression (string_literal) @test.name))) @test.definition)

((unittest_declaration
   (at_attribute
     (identifier) @_uda
     (arguments . (expression (string_literal) @test.name)))
   (:eq? @_uda "Name")) @test.definition)
