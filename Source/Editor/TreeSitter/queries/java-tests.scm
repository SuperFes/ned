;; Test-discovery query (ned-local "@test.definition"/"@test.name" capture
;; convention -- see Mode::testDiscovery in Editor/Mode.h and cpp-tests.scm's
;; own header comment). JUnit 4 and JUnit 5 both mark a test method with an
;; annotation, never a naming convention: JUnit 4's @Test, JUnit 5's @Test
;; plus its parameterized/repeated/dynamic variants (@ParameterizedTest,
;; @RepeatedTest, @TestFactory, @TestTemplate). TestNG's own @Test spells the
;; same bare name, so it is covered by the same entry. Checked against
;; tree-sitter-java's own node-types.json: an annotation is never a direct
;; child of method_declaration -- it lives inside the method's "modifiers"
;; node alongside "public"/"static"/etc, and comes in two node types
;; depending on whether it carries arguments ("marker_annotation" for a bare
;; @Test, "annotation" for @Test(expected = ...)), which is why both shapes
;; are matched here. Matched against the annotation's own bare name only
;; (never a fully-qualified "org.junit.Test") -- the same "checked directly,
;; narrow v1 scope" cut csharp-tests.scm's own attribute-name match makes.
(
  (method_declaration
    (modifiers
      [
        (marker_annotation
          name: (identifier) @_attr)
        (annotation
          name: (identifier) @_attr)
      ])
    name: (identifier) @test.name) @test.definition
  (#any-of? @_attr "Test" "ParameterizedTest" "RepeatedTest" "TestFactory" "TestTemplate")
)
