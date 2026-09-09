;; Test-discovery query (ned-local "@test.definition"/"@test.name" capture
;; convention -- see Mode::testDiscovery in Editor/Mode.h and cpp-tests.scm's
;; own header comment). Kotlin has no test framework of its own: kotlin.test
;; is a thin multiplatform facade whose JVM implementation maps straight onto
;; JUnit, so a Kotlin test is marked with exactly the annotations java-tests.
;; scm already lists -- @Test and JUnit 5's parameterized/repeated/dynamic
;; variants -- and the two files' #any-of? sets are deliberately identical.
;; Checked against fwcd/tree-sitter-kotlin's own node-types.json/grammar.js:
;; an annotation lives inside the function's "modifiers" node (never a direct
;; child of function_declaration), and the annotation's own name arrives as
;; either a bare "user_type" (@Test) or, when it carries arguments, a
;; "constructor_invocation" wrapping one (@Test(expected = ...)) -- hence the
;; two shapes matched here. A qualified "@kotlin.test.Test" parses as one
;; user_type with several type_identifier children, so matching the bare
;; trailing name still lands, the same "bare name only" cut csharp-tests.scm
;; and java-tests.scm both make.
(
  (function_declaration
    (modifiers
      (annotation
        [
          (user_type
            (type_identifier) @_attr)
          (constructor_invocation
            (user_type
              (type_identifier) @_attr))
        ]))
    (simple_identifier) @test.name) @test.definition
  (#any-of? @_attr "Test" "ParameterizedTest" "RepeatedTest" "TestFactory" "TestTemplate")
)
