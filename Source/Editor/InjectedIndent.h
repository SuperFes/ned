//
// injected-region-indentation follow-up: indentation that follows a
// grammar's own language injections, so the code inside an injected region
// is indented by the language it is actually written in.
//
// A host grammar sees an injected region as one opaque token -- PHP's whole
// HTML body is a single `text` node, HTML's <script> body a single
// `raw_text` -- so the host's own indent query has nothing inside it to
// count, and every line of it answered the region's own base column. For a
// PHP template, which is mostly HTML with a few `<?= ?>` islands, that is
// the whole file at column 0, and `indent-buffer`/`ned --format` did not
// merely fail to indent it, it flattened the indentation already there.
//
// The rule this applies is a composition, not a replacement: the host still
// says where the region sits, and the injected language says how far into
// its own structure a line is.
//
//     column = host(line) + inner(line) - inner(the region's first line)
//
// Both halves are needed. HTML's <script> body wants the host's answer (the
// <script> element's own body indent) plus the JavaScript nesting inside it;
// a PHP template wants the host's answer (zero -- the HTML is at top level)
// plus the HTML nesting. Subtracting the region's own first line is what
// makes the injected language's absolute columns relative to wherever the
// host put the region.
//
// Regions are turned into one width-preserving virtual document per
// language (Editor/EmbeddedDocuments.h's BuildInjectedDocuments, the same
// padding the LSP-sync path already uses), so every byte offset, line
// boundary and column in the injected parse is the host buffer's own -- no
// coordinate mapping anywhere in here. Merging a language's regions into one
// document is also what lets HTML split across `<?= ?>` islands read as one
// document rather than a sequence of unbalanced fragments.
//

#ifndef NED_EDITOR_INJECTEDINDENT_H
#define NED_EDITOR_INJECTEDINDENT_H

#include "Mode.h"

namespace ned::editor {

// Process-wide toggle, ned/set-indent-injected-regions -- TabWidth.h's
// mutex-guarded static-storage shape, the convention every process-wide
// setting here follows. Default true: this is what makes a template file
// indent at all. Off restores host-grammar-only indentation.
void               SetIndentInjectedRegions(bool enabled);
[[nodiscard]] bool IndentInjectedRegions();

// Wraps `host` so a line owned by an injected region is indented by that
// region's language instead. Returns `host` unchanged when `regions` is
// unset (a grammar with no injections query at all -- most of them), so the
// ordinary single-language path costs nothing, not even a call frame.
//
// The returned closure caches the virtual documents it builds against the
// exact bufferText it last saw, which is what keeps a whole-buffer reindent
// (IndentRegion's own frozen-text batch loop) to one synthesis rather than
// one per line.
[[nodiscard]] IndentFunction WithInjectedRegionIndent(IndentFunction host, EmbeddedRegionFunction regions);

} // namespace ned::editor

#endif // NED_EDITOR_INJECTEDINDENT_H
