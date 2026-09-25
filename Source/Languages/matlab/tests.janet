#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). matlab.unittest: every method in a `methods (Test)` block,
#; whatever its name; and in a function-based test file (its main function
#; calls functiontests), each later local function whose name starts or ends
#; with "test", case-insensitively. Script-based `%%` sections are left out:
#; MATLAB's mapping from a section title to the name it runs by isn't
#; documented.

((methods
   (attributes (attribute (identifier) @_attribute))
   (function_definition name: (identifier) @test.name) @test.definition)
 (:eq? @_attribute "Test"))

((source_file
   (function_definition
     (block (assignment (function_call name: (identifier) @_functiontests))))
   (function_definition name: (identifier) @test.name) @test.definition)
 (:eq? @_functiontests "functiontests")
 (:match? @test.name "^[Tt][Ee][Ss][Tt]|[Tt][Ee][Ss][Tt]$"))
