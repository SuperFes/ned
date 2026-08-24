;; Test-discovery query (ned-local "@test.definition"/"@test.name" capture
;; convention -- see Mode::testDiscovery in Editor/Mode.h and
;; cpp-tests.scm's own header comment). PHPUnit's collection rules: test*-
;; named public methods, #[Test]-attributed methods (PHPUnit 10+'s
;; attribute style), and *Test-named classes (the class marker also gives
;; run-test-at-point a whole-class target). Node/field names checked
;; against the bundled tree-sitter-php's real parse (method_declaration
;; carries "attributes: (attribute_list (attribute_group (attribute
;; (name))))"). A method matched by both patterns dedupes in Mode.cpp's
;; closure.

((method_declaration
   name: (name) @test.name
   (#match? @test.name "^test")) @test.definition)

((method_declaration
   attributes: (attribute_list (attribute_group (attribute (name) @_attr)))
   name: (name) @test.name
   (#eq? @_attr "Test")) @test.definition)

((class_declaration
   name: (name) @test.name
   (#match? @test.name "Test$")) @test.definition)
