; Ned's own org-mode highlight query -- hand-written against
; nvim-orgmode/tree-sitter-org's real grammar structure (as forked at
; /Development/NED/tree-sitter-ned-org, see that repo's own README.md and
; CMakeLists.txt's ned_add_treesitter_grammar(tree-sitter-org ...) call),
; not borrowed from any nvim-orgmode/vim-ecosystem query file. Both the raw
; grammar's own example queries/highlights.scm and the real, production
; nvim-orgmode/orgmode Neovim plugin's queries/org/highlights.scm depend on
; tree-sitter predicates (#match?/#eq?, or Neovim-Lua-only custom ones like
; #org-is-headline-level?) for their most important distinctions (headline
; level, TODO vs. DONE). Query.cpp's own Captures() does now evaluate the
; common predicates (generic-tree-sitter-highlighting follow-up -- #eq?/
; #match?/#any-of?/#has-ancestor?/etc., see its own header comment), but
; #org-is-headline-level? specifically is a Neovim-Lua-only custom
; predicate no C++ consumer could run regardless -- headline level and
; TODO/DONE are still resolved directly in Mode.cpp's OrgMode() instead
; (see below), unrelated to this file staying structural-only otherwise:
; one pattern per construct, never two patterns that could both match the
; same node (which is exactly what predicates would otherwise be needed to
; disambiguate).
;
; "org.headline.stars" and "org.keyword.candidate" are deliberately NOT
; generic capture names -- Mode.cpp's OrgMode() resolves both directly in
; C++ (star count -> cyclic heading level; keyword text checked against
; org::TodoKeywords(), reusing this project's own existing configuration
; rather than hardcoding "TODO"/"DONE" here) rather than through the shared
; CaptureTable() every other capture below goes through unchanged.
;
; Real Org's own emphasis markers (bold/italic/underline/verbatim/code/
; strikethrough) are genuine grammar nodes here, not delimiter-character
; matching -- see the fork's own src/scanner.c for why upstream couldn't
; support that at all and how this fork adds it.

(headline (stars) @org.headline.stars)
(item . (expr) @org.keyword.candidate)

(tag_list (tag) @tag)
(checkbox) @checkbox
(comment) @comment

(property name: (expr) @attribute)
(drawer name: (expr) @attribute)

(directive name: (expr) @keyword)
(block name: (expr) @keyword)
(block end_name: (expr) @keyword)
(dynamic_block name: (expr) @keyword)
(dynamic_block end_name: (expr) @keyword)

(hr) @punctuation
(timestamp) @constant

(bold) @strong
(italic) @emphasis
(underline) @underline
(verbatim) @string
(code) @string
(strikethrough) @strikethrough
