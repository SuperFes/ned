# Section breadcrumbs for sticky scroll and the symbol gutter.
# tree-sitter-markdown's own grammar wraps each heading and everything
# through the next equal-or-shallower heading in a real "section" node that
# nests by level (its repeat only ever admits strictly deeper sub-sections
# -- confirmed against the vendored grammar source), so a section's own
# range is exactly the containment sticky scroll needs. @definition.module
# resolves to SymbolKind::Namespace, whose section-sign glyph already reads
# as "section". The heading's inline is the @name -- what keeps a nested
# section from being collapsed into its parent by the symbol pipeline's
# same-name/same-kind containment dedup. The anchor is load-bearing: only
# the section's OWN heading (its first child) names it -- a shallower
# setext heading the grammar leaves loose inside a deeper section would
# otherwise mint a duplicate marker of the whole enclosing section per
# heading (observed on the oracle corpus, not hypothetical).

# An ATX heading's closing `##` is not part of its name.
((section . (atx_heading (inline) @name)) @definition.module
  (:set! name.closing-sequence "#"))
((section . (setext_heading (paragraph (inline) @name))) @definition.module)

# The grammar starts a section only at an ATX heading, so a setext heading
# past a section's start is loose inside it: it names just itself. One that
# does start a section is folded into that section's marker by the same-name
# containment dedup.
((setext_heading (paragraph (inline) @name)) @definition.module)
