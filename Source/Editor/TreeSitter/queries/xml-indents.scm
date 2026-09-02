; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention, and html-indents.scm's own comment for why the closing
; marker ("ETag") is captured as a whole NAMED node rather than a single
; anonymous token -- same shape as HTML's "end_tag", confirmed via a real
; parse dump. Checked against tree-sitter-xml's own (xml subdir)
; node-types.json.
(element) @indent

(element (ETag) @dedent)
