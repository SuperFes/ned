# smart-indentation follow-up. See c-indents.scm's own header comment for the
# general convention. HTML has no brace/bracket delimiters -- "element"
# nesting IS the indent structure, and its closing marker ("end_tag", e.g.
# "</div>") is a whole NAMED node, not a single anonymous token the way "}"
# is elsewhere -- confirmed via a real parse dump, not assumed (this is what
# Editor/Indent.cpp's dedent-resolution walk is generalized to handle: it
# walks up by real node identity to find whichever node the query actually
# captured, rather than assuming the smallest node at that byte position IS
# the captured one). script_element/style_element are html's own dedicated
# node types for <script>/<style> (embedded-language-documents follow-up
# handles their CONTENT's own LSP sync separately; this only indents the
# element wrapper itself, same as any other element).
# Constrained to elements that HAVE an opening tag rather than plain
# `(element)`: a self-closing one ("<input .../>") is an `element` too, whose
# only child is `self_closing_tag`, and it has no interior to indent -- left
# unconstrained it counted itself as a level and pushed its own line one
# deeper than its siblings. The engine's walk-start promotion handles the
# opening-tag case (Editor/Indent.cpp) but deliberately knows only
# "start_tag"/"STag", which is the right place to stop teaching it node
# types -- whether a shape has an interior at all is the query's business.
(element (start_tag)) @indent
(script_element) @indent
(style_element) @indent

(element (end_tag) @dedent)
(script_element (end_tag) @dedent)
(style_element (end_tag) @dedent)
