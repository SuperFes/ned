; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. HTML has no brace/bracket delimiters -- "element"
; nesting IS the indent structure, and its closing marker ("end_tag", e.g.
; "</div>") is a whole NAMED node, not a single anonymous token the way "}"
; is elsewhere -- confirmed via a real parse dump, not assumed (this is what
; Editor/Indent.cpp's dedent-resolution walk is generalized to handle: it
; walks up by real node identity to find whichever node the query actually
; captured, rather than assuming the smallest node at that byte position IS
; the captured one). script_element/style_element are html's own dedicated
; node types for <script>/<style> (embedded-language-documents follow-up
; handles their CONTENT's own LSP sync separately; this only indents the
; element wrapper itself, same as any other element).
(element) @indent
(script_element) @indent
(style_element) @indent

(element (end_tag) @dedent)
(script_element (end_tag) @dedent)
(style_element (end_tag) @dedent)
