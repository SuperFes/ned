;; Symbol-kind query (the ctags/nvim-treesitter "@definition.*"/"@name"
;; convention -- see Mode.h's SymbolKindFromCaptureName). Hand-written and
;; repo-local, unlike every other bundled language's tags.scm, because
;; fwcd/tree-sitter-kotlin ships a queries/highlights.scm and nothing else --
;; the same reason c-tags.scm/cpp-tags.scm are repo-local, arrived at from
;; the opposite direction (theirs correct an ambiguous upstream file, this
;; one substitutes for an absent one).
;;
;; Checked against the grammar's own node-types.json directly: a "class"
;; declaration, an "interface" declaration and an "enum class" are all one
;; node type here ("class_declaration"), distinguished only by a modifier
;; keyword the symbol gutter doesn't render differently anyway -- so all
;; three, plus "object"/"companion object" declarations, map to
;; @definition.class. The name is the declaration's own direct
;; "type_identifier" child; a supertype named in a delegation_specifier is
;; nested one level deeper and so never matches. A function's name is
;; likewise its own direct "simple_identifier" child (parameters live inside
;; function_value_parameters), and a property's is nested in its
;; variable_declaration.
(class_declaration
  (type_identifier) @name) @definition.class

(object_declaration
  (type_identifier) @name) @definition.class

(companion_object
  (type_identifier) @name) @definition.class

(function_declaration
  (simple_identifier) @name) @definition.function

(property_declaration
  (variable_declaration
    (simple_identifier) @name)) @definition.var
