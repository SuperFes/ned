#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). package:test's test()/group() and flutter_test's
#; testWidgets(), named by their first argument; the callback keeps the
#; body inside the statement.

((expression_statement
   (identifier) @_fn
   (selector
     (argument_part
       (arguments
         .
         (argument
           (string_literal) @test.name))))) @test.definition
 (:any-of? @_fn "test" "group" "testWidgets"))
