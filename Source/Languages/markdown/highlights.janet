# Ned's own additions beside the vendored upstream query (upstream/
# highlights.janet, concatenated before this file by discovery order).
#
# Heading levels: a plain capture can say which level, because the level IS
# which marker child is present -- no arithmetic needed, unlike Org's
# star-counting (a classifier, see Plugins/languages.janet). The capture
# sits on the whole heading node (through its trailing newline), the same
# whole-line span the old hand-rolled pass produced; upstream's @text.title
# on the inline is suppressed in language.janet so the title text keeps the
# heading's own wash. Levels 4-6 cycle back to 1-3, the project's curated
# three-level convention (SyntaxClass::HeadlineLevel1's own doc comment).

((atx_heading (atx_h1_marker)) @ned.headline-level1)
((atx_heading (atx_h2_marker)) @ned.headline-level2)
((atx_heading (atx_h3_marker)) @ned.headline-level3)
((atx_heading (atx_h4_marker)) @ned.headline-level1)
((atx_heading (atx_h5_marker)) @ned.headline-level2)
((atx_heading (atx_h6_marker)) @ned.headline-level3)

((setext_heading (setext_h1_underline)) @ned.headline-level1)
((setext_heading (setext_h2_underline)) @ned.headline-level2)

# GFM task-list checkboxes -- "checkbox" is already a shared CaptureTable
# name (Org's own query uses it), so no :capture-classes entry needed.
[
  (task_list_marker_checked)
  (task_list_marker_unchecked)
] @checkbox
