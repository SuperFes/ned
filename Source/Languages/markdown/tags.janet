# Section breadcrumbs for sticky scroll and the symbol gutter. Each heading
# names a section; the section's extent -- through the next heading of the
# same or a shallower level -- is computed by the markdown.sections escape,
# since the grammar opens sections only at ATX headings and a setext heading
# is one only once its underline is read. @definition.module resolves to
# SymbolKind::Namespace, whose section-sign glyph already reads as "section".

# An ATX heading's closing `##` is not part of its name.
((atx_heading (inline) @name) @definition.module
  (:set! name.closing-sequence "#"))
((setext_heading (paragraph (inline) @name)) @definition.module)
