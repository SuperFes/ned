//
// The buffer a tracker comment is written in, "*comment KEY*", whose
// keymap-only tracker-comment-mode binds C-c C-c (post) and C-c C-k
// (discard). Which issue it answers is held here keyed by the buffer, not
// by the pane that opened it, like Acp/Compose.h: any pane showing it can
// post it.
//

#ifndef NED_EDITOR_TRACKER_COMMENTBUFFER_H
#define NED_EDITOR_TRACKER_COMMENTBUFFER_H

#include <optional>
#include <string>
#include <string_view>

namespace ned::text {
class Buffer;
} // namespace ned::text

namespace ned::editor::tracker {

inline constexpr std::string_view kCommentModeName = "tracker-comment-mode";

struct CommentTarget {
    std::string connection;
    std::string key;

    bool operator==(const CommentTarget&) const = default;
};

[[nodiscard]] std::string CommentBufferName(const std::string& key);

// Main thread only, like every buffer operation.
void                                       AttachComment(const text::Buffer& buffer, CommentTarget target);
[[nodiscard]] std::optional<CommentTarget> FindComment(const text::Buffer& buffer);
void                                       DetachComment(const text::Buffer& buffer);

// The comment to post: buffer text less surrounding blank lines and
// trailing whitespace; "" for nothing written.
[[nodiscard]] std::string CommentText(std::string_view bufferText);

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_COMMENTBUFFER_H
