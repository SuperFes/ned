;; Test-discovery query (ned-local "@test.definition"/"@test.name" capture
;; convention -- see Mode::testDiscovery in Editor/Mode.h and
;; cpp-tests.scm's own header comment). pytest's default collection rules:
;; test_*-named functions/methods at any nesting, Test*-named classes
;; (which also covers unittest.TestCase subclasses in practice -- their
;; conventional Test* naming, not their base class, is what's matched;
;; a query can't resolve base-class identity anyway).

((function_definition
   name: (identifier) @test.name
   (#match? @test.name "^test")) @test.definition)

((class_definition
   name: (identifier) @test.name
   (#match? @test.name "^Test")) @test.definition)
