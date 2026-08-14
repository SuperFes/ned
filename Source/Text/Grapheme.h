//
// Grapheme-cluster (UAX #29 "user-perceived character") boundaries over a Rope.
//
// Cluster boundaries are computed on demand, not stored in the Rope: an edit
// at a boundary can only change the cluster(s) touching that point, so there's
// nothing to gain from caching boundaries in rope node metadata, and it avoids
// having to keep a fourth aggregate count consistent under every edit.
//

#ifndef NED_TEXT_GRAPHEME_H
#define NED_TEXT_GRAPHEME_H

#include <cstddef>

namespace ned::text {

class Rope;

// Byte offset of the next/previous grapheme cluster boundary relative to
// byteOffset (which itself need not be a boundary). Clamps to [0, rope.ByteLength()].
[[nodiscard]] std::size_t NextGraphemeBoundary(const Rope& rope, std::size_t byteOffset);
[[nodiscard]] std::size_t PreviousGraphemeBoundary(const Rope& rope, std::size_t byteOffset);

// Unlike the two above (which always move to a *different*, strictly
// neighboring boundary), this snaps an arbitrary offset down to itself if
// it's already a boundary, or to the nearest boundary before it otherwise.
// Used at API entry points (e.g. Buffer::SetPoint) that accept a plain byte
// offset from a caller that may not have grapheme-aligned it.
[[nodiscard]] std::size_t SnapToGraphemeBoundary(const Rope& rope, std::size_t byteOffset);

} // namespace ned::text

#endif // NED_TEXT_GRAPHEME_H
