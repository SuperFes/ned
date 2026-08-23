//
// Buffer-local word completion fallback (Emacs dabbrev-expand/Vim
// <C-n>/<C-p>-style) for BufferView's ghost-text pipeline when there's no
// running LSP client for the buffer's language -- unconfigured, failed to
// spawn, or disconnected. UI-agnostic and pure, the same "no dependency on
// Source/UI/" convention FuzzyMatch.h/ProjectSearch.h already establish.
//

#ifndef NED_EDITOR_DABBREVCOMPLETE_H
#define NED_EDITOR_DABBREVCOMPLETE_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

// Scans content (a buffer's full text, the same "whole current text, no
// incremental state" contract HighlightFunction/FoldFunction already use)
// for other alnum/underscore words sharing prefix, case-sensitive,
// ASCII-only (matches BufferView's own WordPrefixStart word-character
// classification -- ned has no Unicode identifier support to fall back on
// here either). Ranked by proximity to point: occurrences before point come
// first (nearest first, mirroring Emacs dabbrev-expand's own backward-then-
// forward search order), then occurrences after point (nearest first);
// deduplicated, capped at maxCandidates. Returns whole matched words, not
// suffixes -- callers strip the already-typed prefix themselves, the same
// way an LSP completion item's insertText is handled. Empty prefix or
// maxCandidates yields no candidates -- nothing meaningful to rank without a
// prefix to anchor on.
[[nodiscard]] std::vector<std::string> CollectDabbrevCandidates(std::string_view content, std::size_t point,
                                                                 std::string_view prefix, std::size_t maxCandidates = 20);

} // namespace ned::editor

#endif // NED_EDITOR_DABBREVCOMPLETE_H
