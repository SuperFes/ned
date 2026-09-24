#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). Patterns follow the real parse of each framework's
#; unexpanded macros, dumped from the bundled grammar.

#; Unity and CMocka: a plain top-level function whose name carries Unity's
#; runner-generator prefixes (test/spec/should). CMocka's are test_ by
#; convention and take `void **state`.
((translation_unit
   (function_definition
     type: (primitive_type)
     declarator: (function_declarator
       declarator: (identifier) @test.name)) @test.definition)
 (:match? @test.name "^(test|spec|should)"))

#; Check: START_TEST(name) { ... } END_TEST. The first parses as a function
#; whose declarator is the parenthesized name; every later one fuses with
#; the END_TEST before it (`END_TEST START_TEST(name) {` -- a function of
#; type END_TEST), so only its declarator is the definition, which the
#; c.test-body escape widens over the body.
((function_definition
   type: (type_identifier) @_macro
   declarator: (parenthesized_declarator
     (identifier) @test.name)) @test.definition
 (:eq? @_macro "START_TEST"))

((function_declarator
   declarator: (identifier) @_macro
   parameters: (parameter_list
     .
     (parameter_declaration
       type: (type_identifier) @test.name)
     .)) @test.definition
 (:eq? @_macro "START_TEST"))

#; Criterion: Test(suite, name) { ... } -- a call statement with the body
#; left as a sibling, which the c.test-body escape re-attaches. The name is
#; the second argument, as Criterion reports it (suite::name).
((call_expression
   function: (identifier) @_macro
   arguments: (argument_list
     (identifier)
     .
     (identifier) @test.name)) @test.definition
 (:any-of? @_macro "Test" "ParameterizedTest" "Theory"))

#; greatest: TEST name(void) { ... } -- TEST parses as the return type.
((function_definition
   type: (type_identifier) @_macro
   declarator: (function_declarator
     declarator: (identifier) @test.name)) @test.definition
 (:eq? @_macro "TEST"))
