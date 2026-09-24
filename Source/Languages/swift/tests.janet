#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). XCTest's test*-named methods in an XCTestCase subclass, and
#; swift-testing's @Test functions and @Suite types.

((class_declaration
   name: (type_identifier) @test.name
   (inheritance_specifier
     inherits_from: (user_type
       (type_identifier) @_base))) @test.definition
 (:eq? @_base "XCTestCase"))

((class_declaration
   body: (class_body
     (function_declaration
       name: (simple_identifier) @test.name) @test.definition))
 (:match? @test.name "^test"))

((function_declaration
   (modifiers
     (attribute
       (user_type
         (type_identifier) @_attribute)))
   name: (simple_identifier) @test.name) @test.definition
 (:eq? @_attribute "Test"))

((class_declaration
   (modifiers
     (attribute
       (user_type
         (type_identifier) @_attribute)))
   name: (type_identifier) @test.name) @test.definition
 (:eq? @_attribute "Suite"))
