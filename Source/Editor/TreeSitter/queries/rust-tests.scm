;; Test-discovery query (ned-local "@test.definition"/"@test.name" capture
;; convention -- see Mode::testDiscovery in Editor/Mode.h and cpp-tests.scm's
;; own header comment). Checked against tree-sitter-rust's own
;; node-types.json: unlike PHP's attribute_list (a real field of
;; method_declaration), Rust's attribute_item is an ordinary PRECEDING
;; SIBLING statement of the function_item it annotates, not a child/field of
;; it -- so this matches by sibling adjacency instead. #[test] is the
;; standard library's own marker; the #match? regex also covers a
;; framework's own scoped variant (#[tokio::test], #[async_std::test],
;; #[actix_rt::test], ...) via the "::test" suffix, the same "checked
;; directly, not guessed" bar every query file in this project holds to.
;;
;; Two patterns, covering up to one other attribute stacked between #[test]
;; and the function (a real, common case -- e.g. "#[test]\n#[should_panic]\n
;; fn ..." puts #[should_panic] as the immediate sibling, not #[test]
;; itself): the immediate-predecessor case, and the one-attribute-removed
;; case in either order. A test function with two or more OTHER attributes
;; stacked between #[test] and itself is a real, narrower v1 cut -- not
;; attempted, revisit if it bites.

(
  (attribute_item (attribute) @_attr)
  .
  (function_item name: (identifier) @test.name) @test.definition
  (#match? @_attr "(^|::)test$")
)

(
  (attribute_item (attribute) @_attr)
  .
  (attribute_item)
  .
  (function_item name: (identifier) @test.name) @test.definition
  (#match? @_attr "(^|::)test$")
)
