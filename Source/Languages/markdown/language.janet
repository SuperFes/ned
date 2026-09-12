# No :line-comment -- Markdown has no comment-line convention of its own.
# Prose wraps (WrapOverrides.h is the per-file override). Fenced code blocks
# are highlighted as their language but not synced to its server.

{:name "markdown"
 :extensions [".md" ".markdown"]
 :wrap-lines true

 # Real Org's own table-editing bindings, mirrored. S-TAB is unbound
 # globally; M-UP/M-DOWN deliberately shadow move-line-up/down with a
 # metaup/metadown that falls back to the same line move outside a table.
 # Every Meta chord gets the dual M-/ESC-prefix binding the global keymap
 # uses ("cover both real input shapes").
 :keymap [
   ["TAB" "markdown-table-align"]
   ["S-TAB" "markdown-table-previous-cell"]
   ["M-UP" "markdown-metaup"]
   ["ESC UP" "markdown-metaup"]
   ["M-DOWN" "markdown-metadown"]
   ["ESC DOWN" "markdown-metadown"]
   ["M-S-DOWN" "markdown-table-insert-row"]
   ["ESC S-DOWN" "markdown-table-insert-row"]
   ["M-S-UP" "markdown-table-kill-row"]
   ["ESC S-UP" "markdown-table-kill-row"]
   ["M-S-RIGHT" "markdown-table-insert-column"]
   ["ESC S-RIGHT" "markdown-table-insert-column"]
   ["M-S-LEFT" "markdown-table-delete-column"]
   ["ESC S-LEFT" "markdown-table-delete-column"]
   ["M-LEFT" "markdown-table-move-column-left"]
   ["ESC LEFT" "markdown-table-move-column-left"]
   ["M-RIGHT" "markdown-table-move-column-right"]
   ["ESC RIGHT" "markdown-table-move-column-right"]]

 # List markers, thematic breaks, heading and blockquote markers get the
 # dimmed MarkupMarker treatment here; every other grammar's use of the
 # same capture name stays Punctuation. The ned.headline-* captures are
 # this language's own whole-heading patterns (highlights.janet), and
 # upstream's @text.title is suppressed so a heading's title text keeps
 # the heading's own wash instead of being re-classed out of it.
 :capture-classes {"punctuation.special" :markup-marker
                   "ned.headline-level1" :headline-level-1
                   "ned.headline-level2" :headline-level-2
                   "ned.headline-level3" :headline-level-3
                   "text.title" :suppress}

 # Hanging list indent is a real tree walk (a bullet's content COLUMN, not
 # a level) -- the one Markdown fact still in C++ (Languages/Markdown.cpp).
 :escapes ["markdown.indent"]
 :snippets
 {
   "link"
   "[${1:text}](${2:url})$0"
   "img"
   "![${1:alt}](${2:url})$0"
   "code"
   "```${1:language}\n$0\n```"}
}
