#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; VUnit's `if run("name") then ... elsif run("other") then`, each branch
#; one test case.

((if_statement
   (simple_expression
     (name
       (identifier) @_fn
       (parenthesis_group
         (association_or_range_list
           .
           (association_element (conditional_expression (simple_expression (string_literal) @test.name)))))))
   (:match? @_fn "^[Rr][Uu][Nn]$")) @test.definition)

((elsif_statement
   (simple_expression
     (name
       (identifier) @_fn
       (parenthesis_group
         (association_or_range_list
           .
           (association_element (conditional_expression (simple_expression (string_literal) @test.name)))))))
   (:match? @_fn "^[Rr][Uu][Nn]$")) @test.definition)
