;; Symbol-kind delta for TypeScript/TSX, concatenated AFTER
;; tree-sitter-javascript's queries/tags.scm and tree-sitter-typescript's own
;; (CMakeLists.txt's ned_embed_treesitter_query_concat) -- see php-tags.scm's
;; header for the delta convention and Mode.h's SymbolKindFromCaptureName for
;; the capture vocabulary.
;;
;; The javascript file is part of that concatenation for a reason worth
;; stating plainly: tree-sitter-typescript's tags.scm is a DELTA ON
;; JAVASCRIPT'S, not a standalone query. It contains function_signature,
;; method_signature, abstract_method_signature, abstract_class_declaration,
;; module and interface_declaration -- and no class_declaration or
;; function_declaration at all, because upstream consumers load javascript's
;; first. Embedded alone, as ned did until this follow-up, a plain
;; "class Widget {}" or "function go() {}" in a .ts file produced no symbol
;; marker whatsoever: an empty symbol gutter, an empty sticky-scroll
;; breadcrumb, and nothing for Editor/ClassFileSync.h to match a filename
;; against. Both upstream files parse cleanly against the tsx grammar too, so
;; TsxMode shares this exactly as it shares every other TypeScript query.
;;
;; ned's own addition is the one below. "namespace App {}" parses as
;; internal_module, a different node type from the "module App {}" form
;; upstream matches -- and `namespace` is how the construct is spelled in
;; TypeScript written today (`module` is the legacy spelling, discouraged
;; since 1.5). The nested_identifier alternative covers the qualified
;; "namespace A.B {}" form, whose name is one node rather than two.
;;
;; @definition.module resolves to SymbolKind::Namespace, not TypeLike -- see
;; SymbolKindFromCaptureName's own body for why that distinction is
;; load-bearing rather than cosmetic.
(internal_module
  name: [(identifier) (nested_identifier)] @name) @definition.module
