//
// The small value types the row painting passes around -- see
// Docs/BufferViewDecomposition.md.
//
// These began as file-local helpers inside BufferView.cpp and were carried into
// BufferView/Internal.h when that file was split. They are not helpers any more:
// the painting, the viewport's wrap arithmetic, and the per-line render state
// all name them, so they get a header of their own rather than being reached
// through the file-local-helper one.
//

#ifndef NED_UI_BUFFERVIEW_RENDERTYPES_H
#define NED_UI_BUFFERVIEW_RENDERTYPES_H

#include <cstddef>
#include <optional>
#include <string>

#include "UI/Widget.h"

namespace ned::ui::bufferview {

// An Org link that should render COLLAPSED on this line -- one whose own
// [startByte, endByte) does NOT contain point. A link containing point is
// deliberately excluded, which is what makes it fall through to the ordinary
// per-codepoint path and render its raw markup uncollapsed.
struct RenderedLink {
    std::size_t startByte;
    std::size_t endByte; // exclusive
    std::string displayText;
};

// Synthetic cells drawn before the real byte at `byteOffset`, filtered down to
// one line -- the same "filter once per line, consult per codepoint" shape the
// highlight spans and links already use. Unlike a RenderedLink this never
// consumes or replaces real bytes; it is drawn between them.
//
// Two sources feed it and an offset may carry both: an LSP inlay hint supplies
// `label`, and a colour literal beginning at this offset supplies `swatch`
// (Editor/ColorLiteral.h). They share one type, and therefore one span list,
// because every piece of the column arithmetic -- VisualColumn,
// ByteOffsetForColumnInLine, SkipToColumn, the wrap segmentation -- has to
// count these cells or the cursor drifts left of the character it is on. That
// was a real reported bug once already; a second, parallel virtual-text
// mechanism would be a second chance to reintroduce it. Ask
// VirtualTextColumns() for the width, never DisplayColumns(label) directly.
struct RenderedVirtualText {
    std::size_t byteOffset;
    std::string label;
    int         kind = 0; // lsp::InlayHint::kind -- chooses the colour, nothing else
    // The colour a literal here names, drawn as one filled cell ahead of
    // `label`. Unset for anything that is not a colour literal, and for every
    // literal when the swatch style is Underlay (which washes the literal's
    // own cells instead, costing no columns).
    std::optional<Color> swatch;
};

// A colour literal's own byte range, washed in the colour it names -- what
// ColorSwatchStyle::Underlay draws instead of a swatch cell. Deliberately not
// a RenderedVirtualText: it occupies no columns of its own, so none of the
// column arithmetic has to know about it.
struct RenderedColorUnderlay {
    std::size_t startByte;
    std::size_t endByte; // exclusive
    Color       color;
};

// The [startByte, endByte) content range one wrapped canvas row draws. There is
// always at least one per line, even an empty one.
//
// wrap-indent follow-up: continuationIndent is the extra columns (past the
// gutter) this row's own content starts drawing at -- 0 for a line's first
// segment always, and 0 for every segment when Editor::WrapIndent() is off,
// otherwise the line's own leading-whitespace width for every segment
// AFTER the first. Stored per-segment (rather than left for each of
// ComputeWrappedLineSegments' several consumers to re-derive on their own)
// so Paint()'s render loop, CursorPosition(), and ByteOffsetForPoint's
// click resolution can never disagree about where a continuation row's
// content actually starts -- the same "one true source" discipline this
// struct's own doc comment already establishes for startByte/endByte.
struct WrapSegment {
    std::size_t startByte;
    std::size_t endByte;
    int         continuationIndent = 0;
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_RENDERTYPES_H
