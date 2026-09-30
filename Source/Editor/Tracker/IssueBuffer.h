//
// The read-only "*issue KEY*" buffer an issue opens into. Rendered as
// Markdown: GitHub bodies already are, and headings and bullet fields read
// well in markdown-mode for any tracker.
//

#ifndef NED_EDITOR_TRACKER_ISSUEBUFFER_H
#define NED_EDITOR_TRACKER_ISSUEBUFFER_H

#include <string>

#include "Provider.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor::tracker {

[[nodiscard]] std::string IssueBufferName(const std::string& key);

[[nodiscard]] std::string RenderIssue(const IssueDetail& detail);

// Finds or creates the issue's buffer and rewrites it wholesale, so opening
// an issue again shows its current state. Point lands at the top.
text::Buffer& ShowIssue(text::BufferList& bufferList, const IssueDetail& detail);

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_ISSUEBUFFER_H
