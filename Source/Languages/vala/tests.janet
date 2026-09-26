#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; GLib's `Test.add_func ("/path/name", fn)` (and add_data_func), and the
#; `add_test ("name", fn)` of the TestCase helper most Vala projects copy
#; from GLib's own. The path is what `-p` selects.

((method_call_expression
   (member_access_expression (member_access_expression) @_owner . (identifier) @_fn)
   .
   (argument (literal (string) @test.name))
   (:match? @_owner "(^|\\.)Test$")
   (:any-of? @_fn "add_func" "add_data_func")) @test.definition)

((method_call_expression
   (member_access_expression . (identifier) @_fn .)
   .
   (argument (literal (string) @test.name))
   (:eq? @_fn "add_test")) @test.definition)
