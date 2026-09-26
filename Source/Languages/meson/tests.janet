#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; test('name', exe) and benchmark('name', exe); `meson test <name>` runs
#; one.

((normal_command
   (identifier) @_fn
   .
   (variableunit (string) @test.name)
   (:any-of? @_fn "test" "benchmark")) @test.definition)
