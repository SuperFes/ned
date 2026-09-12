#; Test-discovery query (ned-local "@test.definition"/"@test.name" capture
#; convention -- see Mode::testDiscovery in Editor/Mode.h and cpp-tests.scm's
#; own header comment). C#'s three mainstream frameworks all mark a test
#; method with an attribute, never a naming convention: xUnit's [Fact]/
#; [Theory], NUnit's [Test]/[TestCase]/[TestCaseSource], MSTest's
#; [TestMethod]/[DataTestMethod]. Checked against tree-sitter-c-sharp's own
#; node-types.json: attribute_list is a real, direct (unnamed) CHILD of
#; method_declaration -- neither a field (PHP's own attribute_list shape)
#; nor a preceding sibling (Rust's attribute_item shape) -- so this matches
#; by ordinary child containment, and (per tree-sitter query semantics) the
#; pattern still matches when the attribute lives in one of several
#; separate "[Attr1] [Attr2]"-style bracket groups, not just when it's the
#; sole entry in "[Attr1, Attr2]". Matched against the attribute's own bare
#; name only (never a fully-qualified "Xunit.FactAttribute") -- the same
#; "checked directly, narrow v1 scope" cut cpp-tests.scm's own macro-name
#; match makes.
(
  (method_declaration
    (attribute_list
      (attribute
        name: (identifier) @_attr))
    name: (identifier) @test.name) @test.definition
  (:any-of? @_attr "Fact" "Theory" "Test" "TestCase" "TestCaseSource" "TestMethod" "DataTestMethod")
)
