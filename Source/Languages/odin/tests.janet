#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). core:testing's @(test) procedures.

((procedure_declaration
   (attributes (attribute (identifier) @_attribute))
   .
   (identifier) @test.name) @test.definition
 (:eq? @_attribute "test"))
