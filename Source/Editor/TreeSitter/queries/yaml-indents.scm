; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. YAML's block_mapping/block_sequence have no closing
; delimiter of any kind (pure indentation-defined structure) -- same shape
; as Python's own "block" (confirmed via a real parse dump: a nested
; mapping's own StartByte coincides with its first pair's, exactly like
; Python's block/if_statement coincidence Editor/Indent.cpp's
; node-identity-keyed capture set and self-exclusion-by-identity walk
; already handle correctly). Unlike Python's "block" -- which only ever
; wraps a genuinely NESTED body -- YAML uses the exact same node type for
; the document's own ROOT mapping/sequence too, with no grammatical
; distinction; capturing every block_mapping/block_sequence unconditionally
; would double-count the root as its own indent level. #has-ancestor? (the
; one predicate Query.cpp evaluates for this, see Query.h's own doc comment)
; restricts each capture to instances genuinely NESTED inside another
; block_mapping/block_sequence, excluding the document root. Checked against
; tree-sitter-yaml's own node-types.json plus a real parse dump. Multi-line
; flow collections ("{...}"/"[...]" spanning several lines) are a deliberate
; v1 gap -- rare in practice, and this simple grammar variant doesn't parse
; them cleanly enough to test with confidence.
((block_mapping) @indent (#has-ancestor? @indent block_mapping))
((block_mapping) @indent (#has-ancestor? @indent block_sequence))
((block_sequence) @indent (#has-ancestor? @indent block_mapping))
((block_sequence) @indent (#has-ancestor? @indent block_sequence))
