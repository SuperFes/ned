;; Symbol-kind delta for PHP, concatenated after tree-sitter-php's own
;; queries/tags.scm (CMakeLists.txt's ned_embed_treesitter_query_concat) --
;; the ctags/nvim-treesitter "@definition.*"/"@name" convention, see Mode.h's
;; SymbolKindFromCaptureName.
;;
;; Unlike c-tags.scm/cpp-tags.scm (which replace an ambiguous upstream file)
;; and kotlin-tags.scm (which substitutes for an absent one), this only adds
;; to a file that is otherwise consumed unmodified -- so a grammar bump keeps
;; bringing upstream's own corrections along.
;;
;; Upstream tags class/interface/trait/function/method/field and the
;; namespace, but predates PHP 8.1's enum -- which is ordinary in code
;; written today, and which Editor/ClassFileSync.h needs in order to rename
;; Status.php after the enum inside it. Checked against the real grammar
;; (v0.24.2): enum_declaration carries its name in a "name:" field of node
;; type "name", exactly as class_declaration does.
(enum_declaration
  name: (name) @name) @definition.enum
