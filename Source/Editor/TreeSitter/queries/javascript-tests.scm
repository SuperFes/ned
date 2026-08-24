;; Test-discovery query (ned-local "@test.definition"/"@test.name" capture
;; convention -- see Mode::testDiscovery in Editor/Mode.h and
;; cpp-tests.scm's own header comment). jest/vitest/mocha's shared shape:
;; it("name", fn) / test("name", fn) / describe("name", fn), plus the
;; member-called modifiers (it.only / test.skip / describe.each). The
;; callback argument keeps the whole body inside the call_expression's own
;; range, so nesting resolution (an it inside a describe) needs no
;; sibling-reattachment the way cpp-tests.scm's macros do. The "(string)"
;; capture includes its quotes -- StripDelimiters in Mode.cpp's closure
;; removes them.
;;
;; This file is duplicated verbatim as typescript-tests.scm (the same
;; javascript-imports.scm/typescript-imports.scm duplication -- two
;; grammars, same node names for these constructs).

((call_expression
   function: (identifier) @_fn
   arguments: (arguments . (string) @test.name)
   (#any-of? @_fn "it" "test" "describe")) @test.definition)

((call_expression
   function: (member_expression object: (identifier) @_fn)
   arguments: (arguments . (string) @test.name)
   (#any-of? @_fn "it" "test" "describe")) @test.definition)
