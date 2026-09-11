;; Symbol-kind delta for C#, concatenated after tree-sitter-c-sharp's own
;; queries/tags.scm -- see php-tags.scm's header for the delta convention and
;; Mode.h's SymbolKindFromCaptureName for the capture vocabulary.
;;
;; Upstream tags class/interface/method and the block-form namespace, leaving
;; out three type forms that are ordinary in C# written today: enum, struct,
;; and record (record_declaration covers "record struct" too -- confirmed
;; against the real grammar, it is not a separate node type).
;;
;; The fourth pattern is the one that matters most for modern code: a
;; file-scoped "namespace App;" (C# 10+, now the default in new projects)
;; parses as file_scoped_namespace_declaration, a DIFFERENT node type from
;; the block form upstream matches -- so until this it produced no marker at
;; all. It is tagged @definition.module like its block-form sibling, which
;; Mode.h resolves to SymbolKind::Namespace; note it encloses nothing, so a
;; consumer walking containment sees it beside the file's types rather than
;; around them, exactly as PHP's statement-form namespace behaves.
(enum_declaration
  name: (identifier) @name) @definition.enum

(struct_declaration
  name: (identifier) @name) @definition.struct

(record_declaration
  name: (identifier) @name) @definition.class

(file_scoped_namespace_declaration
  name: (identifier) @name) @definition.module
