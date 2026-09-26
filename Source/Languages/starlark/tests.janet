#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; A Bazel/Buck `*_test(name = "x", ...)` rule, or a `test_suite`; the name
#; is the target a `bazel test //pkg:x` runs.

((call
   (identifier) @_rule
   (argument_list
     (keyword_argument
       (identifier) @_key
       (string (string_content) @test.name)))
   (:match? @_rule "(_test|^test_suite)$")
   (:eq? @_key "name")) @test.definition)
