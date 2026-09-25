#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). bats-core's `@test "name" { ... }`.

(bats_test name: (_) @test.name) @test.definition
