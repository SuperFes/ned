#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). testthat's test_that() and its describe()/it().

((call
   function: (identifier) @_fn
   arguments: (arguments . (argument value: (string) @test.name))) @test.definition
 (:any-of? @_fn "test_that" "describe" "it"))
