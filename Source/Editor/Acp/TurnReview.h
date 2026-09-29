//
// Reviewing what one agent turn changed: each file's before/after (Editor/
// Acp/TurnFiles.h) as hunks, each of which can be kept or undone against
// the file as it reads now -- found again by its surrounding lines, so a
// later edit elsewhere in the file doesn't strand it. UI-free; BufferView
// shows a ReviewSession as an *acp review* multibuffer.
//

#ifndef NED_EDITOR_ACP_TURNREVIEW_H
#define NED_EDITOR_ACP_TURNREVIEW_H

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Multibuffer.h"
#include "TurnFiles.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor::acp {

// One change the turn made to one file: `oldLines` became `newLines`, with
// up to kReviewContextLines unchanged lines either side. Lines keep their
// '\n'. A created or deleted file is one hunk covering all of it.
struct ReviewHunk {
    std::size_t              fileIndex = 0;
    std::size_t              oldStart  = 0; // 0-based line in the before text
    std::size_t              newStart  = 0; // 0-based line in the after text
    std::vector<std::string> contextBefore;
    std::vector<std::string> oldLines;
    std::vector<std::string> newLines;
    std::vector<std::string> contextAfter;
    bool                     created = false;
    bool                     deleted = false;
};

inline constexpr std::size_t kReviewContextLines = 3;

inline constexpr std::string_view kReviewBufferName = "*acp review*";
inline constexpr std::string_view kReviewModeName   = "acp-review-mode";

[[nodiscard]] std::vector<ReviewHunk> TurnHunks(const TurnFile& file, std::size_t fileIndex);

// Whether the file still shows the hunk as the turn left it, shows it
// undone, or has changed there since.
enum class HunkState { Applied,
                       Undone,
                       Changed };
[[nodiscard]] HunkState StateOf(const FileState& current, const ReviewHunk& hunk);

// Where the hunk's after-side lines start in `current`, nearest where the
// turn left them; nullopt when they aren't there any more.
[[nodiscard]] std::optional<std::size_t> LocateHunk(const FileState& current, const ReviewHunk& hunk);

// `current` with the hunk's change reversed; nullopt unless it's Applied.
[[nodiscard]] std::optional<FileState> RevertHunk(const FileState& current, const ReviewHunk& hunk);

struct ReviewSession {
    std::string             title; // the turn's prompt
    std::filesystem::path   root;  // paths are shown relative to it
    std::vector<TurnFile>   files;
    std::vector<ReviewHunk> hunks;
    std::vector<bool>       kept; // per hunk
};

[[nodiscard]] ReviewSession MakeReviewSession(std::string title, std::filesystem::path root, std::vector<TurnFile> files);

// Undoes hunk `index`, or every hunk of its file; a short message saying
// what happened. Nothing is written when any of them can't be undone.
std::string UndoHunk(text::BufferList& bufferList, ReviewSession& session, std::size_t index);
std::string UndoFile(text::BufferList& bufferList, ReviewSession& session, std::size_t index);

// One excerpt per hunk, headed by its file, line, size and state.
[[nodiscard]] std::vector<multibuffer::ExcerptSource> ReviewExcerpts(const text::BufferList& bufferList, const ReviewSession& session);

// The review buffers' sessions, keyed by Buffer::InstanceId() -- the same
// registry shape as Acp/Compose.h.
void                         AttachReview(const text::Buffer& buffer, ReviewSession session);
[[nodiscard]] ReviewSession* ReviewFor(const text::Buffer& buffer);
void                         DetachReview(const text::Buffer& buffer);

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_TURNREVIEW_H
