;; Test-discovery query (ned-local "@test.definition"/"@test.name" capture
;; convention -- see Mode::testDiscovery in Editor/Mode.h; there is no
;; upstream tests.scm convention to vendor). Patterns are written against
;; tree-sitter-cpp's real error-recovery parse of *unexpanded* test macros,
;; dumped from the actual bundled grammar, not guessed:
;;
;;   TEST_CASE("name") { ... }  parses as an (expression_statement
;;     (call_expression ...) (MISSING ";")) with the body left as a SIBLING
;;     compound_statement -- Mode.cpp's closure re-attaches that sibling to
;;     the marker's range, nothing this query can express.
;;
;;   TEST(Suite, Name) { ... }  parses as a function_definition whose
;;     "parameters" are two type-only parameter_declarations.

;; Catch2 TEST_CASE("name" [, "[tags]"]) / SCENARIO("name")
((call_expression
   function: (identifier) @_macro
   arguments: (argument_list . (string_literal (string_content) @test.name))
   (#any-of? @_macro "TEST_CASE" "SCENARIO")) @test.definition)

;; Catch2 TEST_CASE_METHOD(Fixture, "name" [, "[tags]"]) -- name is the
;; second argument.
((call_expression
   function: (identifier) @_macro
   arguments: (argument_list (identifier) (string_literal (string_content) @test.name))
   (#eq? @_macro "TEST_CASE_METHOD")) @test.definition)

;; googletest TEST(Suite, Name) / TEST_F(Fixture, Name) / TEST_P(Fixture,
;; Name) -- the name is the second "parameter"; gutter matching's
;; trailing-segment rule relates it to gtest's own "Suite.Name" reporting.
((function_definition
   declarator: (function_declarator
     declarator: (identifier) @_macro
     parameters: (parameter_list
       (parameter_declaration type: (type_identifier))
       (parameter_declaration type: (type_identifier) @test.name)))
   (#any-of? @_macro "TEST" "TEST_F" "TEST_P")) @test.definition)
