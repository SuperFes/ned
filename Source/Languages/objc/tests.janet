#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). XCTest's -test* methods.

((method_definition
   (method_type (type_name (primitive_type) @_type))
   .
   (identifier) @test.name) @test.definition
 (:eq? @_type "void")
 (:match? @test.name "^test"))
