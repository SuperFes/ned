#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). pgTAP's xUnit-style tests: functions runtests() finds by the
#; name pattern ^test (an unquoted name is folded to lower case, so any case
#; counts). Its fixtures (startup, setup, teardown, shutdown) never start
#; with "test".

((create_function
   (object_reference name: (identifier) @test.name)) @test.definition
 (:match? @test.name "^[Tt][Ee][Ss][Tt]"))
