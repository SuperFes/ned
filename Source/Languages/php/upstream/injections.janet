# A PHP file is an HTML document with PHP islands in it, not the other way
# round: everything outside `<?php ... ?>` is one `text` node to this grammar
# (and one more per `text_interpolation`, which is what a `<?= ?>` in the
# middle of a line produces), carrying no structure of its own at all.
# Injecting html is what gives those regions their real nesting back --
# highlighting, embedded-LSP sync, and indentation (Editor/InjectedIndent.h)
# all read it.
#
# Every match resolves to the same language, which is what makes the regions
# on either side of a `<?= ?>` one HTML document rather than a sequence of
# fragments each missing the other's open tags -- see BuildInjectedDocuments
# (Editor/EmbeddedDocuments.h) for the merge.
((text) @injection.content
 (:set! injection.language "html"))
