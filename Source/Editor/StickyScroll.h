//
// Pure, buffer-free enclosing-symbol-chain resolution for the main-editor
// sticky scroll (namespace/class/method breadcrumb) -- built on top of
// Mode::symbolKind's own SymbolMarker landmarks (main-editor-sticky-scroll
// follow-up: SymbolMarker gained a full [startByte, endByte) range and a
// name specifically for this). Every SymbolMarker range comes from a real
// tree-sitter AST node, so two ranges here are always either disjoint or
// properly nested, never partially overlapping -- the same guarantee
// CodeFold.h's own FoldRegionsWithDepth relies on for fold ranges -- which
// is what makes a plain ascending-startByte scan already root-to-leaf, with
// no separate tree/stack structure needed.
//

#ifndef NED_EDITOR_STICKYSCROLL_H
#define NED_EDITOR_STICKYSCROLL_H

#include <cstddef>
#include <utility>
#include <vector>

#include "Mode.h"

namespace ned::editor::stickyscroll {

// Every marker in `markers` (sorted by startByte -- Mode::symbolKind's own
// contract) whose [startByte, endByte) contains `point`, outer-to-inner.
[[nodiscard]] std::vector<SymbolMarker> EnclosingSymbolChain(const std::vector<SymbolMarker>& markers, std::size_t point);

// The sticky-scroll variant: an ancestor only counts as "sticky" once its
// own header has genuinely scrolled above the viewport, not merely that the
// viewport's top line falls somewhere inside its body -- so the start-edge
// comparison is strict (marker.startByte < viewportTopByte), unlike
// EnclosingSymbolChain's inclusive one. viewportTopByte is expected to be
// the exact byte offset of the viewport's current top line's own first
// byte (Buffer::Content().LineToByteOffset(topLine)) -- any marker whose
// definition starts anywhere on that same line, indented or not, then has
// startByte >= viewportTopByte and is correctly excluded: its header is
// still the visible top line, nothing has scrolled away yet. A fold never
// hides its own header line (BufferView only ever hides the body between a
// header and its closing line), so a still-visible header -- collapsed or
// not -- is naturally excluded here too, with no fold-awareness of its own:
// this function only ever sees byte ranges and one offset.
[[nodiscard]] std::vector<SymbolMarker> StickyChainForViewportTop(const std::vector<SymbolMarker>& markers,
                                                                  std::size_t                      viewportTopByte);

// sticky-scroll-from-folds follow-up: the same landmarks, derived from a
// buffer's FOLD structure instead of its tags query.
//
// Ten bundled languages have an imprint table and no `tags.scm` -- yaml, toml,
// json, css, bash, fish, clojure, janet, xml, html -- so `Mode::symbolKind` is
// unset for them and they had no sticky scroll at all. But the fold blocks
// those languages already produce carry exactly the fact this needs, because
// of an invariant the fold work had to establish for its own reasons: **a fold
// block's start byte sits on the row that stays visible when it collapses.**
// That row is, by construction, the row worth pinning -- `root:`, `[package]`,
// `"dependencies": {`, a `function` line.
//
// So this is a third driver off one Tier 0 fact, and it needs no new query,
// no new capability on Mode, and no extra work per frame (BufferView already
// caches the fold blocks for the gutter). The markers carry
// `SymbolKind::Block`: the grammar says "container", and nothing says what
// kind, which is the honest label.
//
// `blocks` is expected to be `CodeFold.h`'s own FoldableBlocks output --
// sorted by startByte, unique starts, and either disjoint or properly nested,
// which is precisely what StickyChainForViewportTop above assumes.
[[nodiscard]] std::vector<SymbolMarker>
MarkersFromFoldBlocks(const std::vector<std::pair<std::size_t, std::size_t>>& blocks);

} // namespace ned::editor::stickyscroll

#endif // NED_EDITOR_STICKYSCROLL_H
