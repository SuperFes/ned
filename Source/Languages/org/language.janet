# "org" is ned's own forked grammar; its highlighting needs the org.*
# escapes (Languages/Org.cpp). "#" is org-comment-string's default.

{:name "org"
 :extensions [".org"]
 :line-comment "#"
 :wrap-lines true

 # Real Org's own bindings, with three deliberate mode-over-global shadows
 # (C-c C-p over toggle-project-sidebar, C-c C-o over find-scratch,
 # C-c C-s/C-c C-d over project-search/create-directory) -- a mode layer
 # overriding the global layer per buffer is what KeymapStack exists for.
 # Clock in/out/report use plain letters under C-c C-x rather than real
 # Org's C-i/C-o: Ctrl-I is byte-identical to Tab over a raw terminal.
 :keymap [
   ["C-c C-t" "org-cycle-todo"]
   ["C-c C-p" "org-cycle-priority"]
   ["C-c C-c" "org-toggle-checkbox"]
   ["TAB" "org-cycle"]
   ["C-c C-q" "org-set-tags"]
   ["C-c C-x p" "org-set-property"]
   ["C-c C-x d" "org-delete-property"]
   ["C-c C-x i" "org-clock-in"]
   ["C-c C-x o" "org-clock-out"]
   ["C-c C-x r" "org-clock-report"]
   ["C-c C-s" "org-schedule"]
   ["C-c C-d" "org-deadline"]
   ["C-c C-o" "open-link-at-point"]
   ["S-TAB" "org-table-previous-cell"]
   ["M-UP" "org-metaup"]
   ["ESC UP" "org-metaup"]
   ["M-DOWN" "org-metadown"]
   ["ESC DOWN" "org-metadown"]
   ["M-S-DOWN" "org-table-insert-row"]
   ["ESC S-DOWN" "org-table-insert-row"]
   ["M-S-UP" "org-table-kill-row"]
   ["ESC S-UP" "org-table-kill-row"]
   ["M-S-RIGHT" "org-table-insert-column"]
   ["ESC S-RIGHT" "org-table-insert-column"]
   ["M-S-LEFT" "org-table-delete-column"]
   ["ESC S-LEFT" "org-table-delete-column"]
   ["M-LEFT" "org-table-move-column-left"]
   ["ESC LEFT" "org-table-move-column-left"]
   ["M-RIGHT" "org-table-move-column-right"]
   ["ESC RIGHT" "org-table-move-column-right"]
   ["C-c -" "org-table-insert-hline"]]

 # Headline level is arithmetic over counted stars and TODO-vs-DONE
 # compares against org/TodoKeywords, runtime-configured state no query
 # predicate can reach -- see Languages/Org.cpp.
 :escapes ["org.highlight" "org.indent" "org.symbols"]
}
