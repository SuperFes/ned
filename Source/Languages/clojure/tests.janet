#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). clojure.test's deftest, and its testing blocks
#; named by their string.

((list_lit
   .
   (sym_lit name: (sym_name) @_head)
   .
   (sym_lit name: (sym_name) @test.name)) @test.definition
 (:eq? @_head "deftest"))

((list_lit
   .
   (sym_lit name: (sym_name) @_head)
   .
   (str_lit) @test.name) @test.definition
 (:eq? @_head "testing"))
