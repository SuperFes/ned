#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). testament's and judge's `(deftest name ...)` -- a symbol, or
#; for judge a string -- and judge's typed `(deftest: type name ...)`.
#; Anonymous tests (judge's top-level `test`, a nameless testament deftest)
#; have no name to run by.

((par_tup_lit
   .
   (sym_lit) @_head
   .
   [(sym_lit) (str_lit)] @test.name) @test.definition
 (:eq? @_head "deftest"))

((par_tup_lit
   .
   (sym_lit) @_head
   .
   (sym_lit)
   .
   [(sym_lit) (str_lit)] @test.name) @test.definition
 (:eq? @_head "deftest:"))
