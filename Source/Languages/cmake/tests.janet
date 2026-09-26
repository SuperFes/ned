#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; CTest's add_test(NAME <name> COMMAND ...) and the older
#; add_test(<name> <command>); `ctest -R <name>` runs one. Command names are
#; case-insensitive.

((normal_command
   (identifier) @_cmd
   (argument_list . (argument) @_keyword . (argument) @test.name)
   (:match? @_cmd "^[Aa][Dd][Dd]_[Tt][Ee][Ss][Tt]$")
   (:eq? @_keyword "NAME")) @test.definition)

((normal_command
   (identifier) @_cmd
   (argument_list . (argument) @test.name)
   (:match? @_cmd "^[Aa][Dd][Dd]_[Tt][Ee][Ss][Tt]$")
   (:not-eq? @test.name "NAME")) @test.definition)
