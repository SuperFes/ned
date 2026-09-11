;; Symbol-kind delta for Java, concatenated after tree-sitter-java's own
;; queries/tags.scm -- see php-tags.scm's header for the delta convention and
;; Mode.h's SymbolKindFromCaptureName for the capture vocabulary.
;;
;; Upstream tags class/interface/method only. A record is a class in every
;; sense this query cares about (a named top-level type declaration), and an
;; enum is a type Java has had since 5 -- both are missing purely because the
;; upstream file is old, not because of any judgement call. Without them a
;; Status.java holding "enum Status" had no symbol marker at all, so the
;; gutter, the sticky-scroll breadcrumb and Editor/ClassFileSync.h each saw
;; an empty file.
;;
;; Checked against the real grammar: both carry their name in a "name:" field
;; of node type "identifier", the same shape class_declaration uses.
(enum_declaration
  name: (identifier) @name) @definition.enum

(record_declaration
  name: (identifier) @name) @definition.class
