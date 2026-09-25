#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). purescript-spec's describe/it.

((exp_apply
   .
   (exp_name (variable) @_fn)
   .
   (exp_literal (string) @test.name)) @test.definition
 (:any-of? @_fn "describe" "it" "pending" "describeOnly" "itOnly"))
