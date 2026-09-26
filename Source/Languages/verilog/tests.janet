#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; A UVM test -- a class extending uvm_test or a project's *_test base --
#; which `+UVM_TESTNAME=<class>` selects; SVUnit's `SVTEST(name)` (its body
#; runs to a sibling `SVTEST_END`, so the marker covers the opening line).

((class_declaration
   (class_identifier) @test.name
   (class_type (class_identifier) @_base)
   (:match? @_base "(^uvm_test|_test)$")) @test.definition)

((text_macro_usage
   (text_macro_identifier) @_macro
   (list_of_actual_arguments . (expression) @test.name)
   (:eq? @_macro "SVTEST")) @test.definition)
