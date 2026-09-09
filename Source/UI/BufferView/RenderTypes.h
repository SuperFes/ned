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
#include <string>

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

// An LSP inlay hint (byte offset + label) filtered down to one line -- the same
// "filter once per line, consult per codepoint" shape the highlight spans and
// links already use. Unlike a RenderedLink this never consumes or replaces real
// bytes; it is drawn between them.
struct RenderedInlayHint {
    std::size_t byteOffset;
    std::string label;
};

// The [startByte, endByte) content range one wrapped canvas row draws. There is
// always at least one per line, even an empty one.
struct WrapSegment {
    std::size_t startByte;
    std::size_t endByte;
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_RENDERTYPES_H
