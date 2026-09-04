//
// Debugging wishlist follow-up (ROADMAP.md's Maybelist, the Valgrind entry):
// renders a parsed MassifOutputParser.h MassifProfile into a read-only
// "*massif report*" buffer -- a heap-usage-over-time sparkline
// (Editor/Sparkline.h, the same substrate DapManager's watch-history graph
// uses) plus a per-snapshot summary table. TestResultsBuffer.h's own
// find-or-create/wholesale-rewrite shape: a fresh graph command supersedes
// the prior report rather than accumulating buffers.
//

#ifndef NED_EDITOR_MASSIFREPORTBUFFER_H
#define NED_EDITOR_MASSIFREPORTBUFFER_H

#include <string>
#include <string_view>

#include "MassifOutputParser.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor {

[[nodiscard]] std::string MassifReportBufferName();

// Finds-or-creates the read-only "*massif report*" buffer and wholesale
// rewrites it from profile. sourcePath is shown in the header only (the
// file the profile was read from), never reparsed here. Point lands at the
// top of the buffer.
text::Buffer& RebuildMassifReportBuffer(text::BufferList& bufferList, const MassifProfile& profile,
                                         std::string_view sourcePath);

} // namespace ned::editor

#endif // NED_EDITOR_MASSIFREPORTBUFFER_H
