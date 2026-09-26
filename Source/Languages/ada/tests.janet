#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; AUnit's `Register_Routine (T, Test_X'Access, "Name")`, plain or
#; qualified; the third argument is the name a run reports and filters by.
#; Ada names are case-insensitive.

((procedure_call_statement
   [(identifier) @_proc (selected_component (identifier) @_proc .)]
   (actual_parameter_part
     .
     (parameter_association)
     .
     (parameter_association)
     .
     (parameter_association (expression (term (string_literal) @test.name))))
   (:match? @_proc "^[Rr][Ee][Gg][Ii][Ss][Tt][Ee][Rr]_[Rr][Oo][Uu][Tt][Ii][Nn][Ee]$")) @test.definition)
