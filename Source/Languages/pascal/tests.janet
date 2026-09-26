#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; FPCUnit and DUnit run a test case's `published` methods, on a class
#; descending from TTestCase (or a project's own *Test* base); DUnitX runs
#; methods carrying a `[Test]` or `[TestCase(...)]` attribute.

((declClass
   (typeref) @_base
   (declSection
     (kPublished)
     (declProc (identifier) @test.name) @test.definition))
 (:match? @_base "Test"))

((declProc
   (rttiAttributes (identifier) @_attribute)
   (identifier) @test.name
   (:any-of? @_attribute "Test" "TestCase")) @test.definition)
