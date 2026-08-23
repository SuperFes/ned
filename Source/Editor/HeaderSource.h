//
// Header/source-switching follow-up. Pure, filesystem-only fallback for
// "jump to the other half of this file" when no LSP client is running, or
// the running one has no answer of its own (see BufferView::SwitchHeaderSource,
// which tries clangd's textDocument/switchSourceHeader extension first --
// LspManager.h/.cpp -- and only reaches this when that's unavailable). C/C++
// (and Objective-C/C++) only: no other bundled language has an equivalent
// split-file convention.
//

#ifndef NED_EDITOR_HEADERSOURCE_H
#define NED_EDITOR_HEADERSOURCE_H

#include <filesystem>
#include <optional>
#include <string>

namespace ned::editor::headersource {

// Case-insensitive extension classification (".H"/".Hpp" count same as
// ".h"/".hpp"). A path can be neither -- e.g. ".cmake", ".txt" -- in which
// case FindCounterpart below returns nullopt outright.
[[nodiscard]] bool IsHeaderExtension(const std::string& extension);
[[nodiscard]] bool IsSourceExtension(const std::string& extension);

// Finds path's counterpart file on disk: same stem, an extension of the
// opposite kind (header -> source or vice versa), tried first in path's own
// directory, then in a small fixed set of sibling-directory name swaps
// common to real C/C++ project layouts (src<->include, source<->include,
// Source<->Include, src<->inc) applied to path's *parent* directory --
// e.g. src/foo.cpp finds include/foo.h. Deliberately one level, not a
// recursive project-wide search: a real counterpart is expected to be one of
// these small number of candidates or not findable heuristically at all
// (LSP, when available, already covers the general case via the actual
// compilation database -- see this file's own header comment). Existence is
// checked directly via std::filesystem; nullopt if path's own extension
// isn't a recognized header/source extension, or no candidate exists.
[[nodiscard]] std::optional<std::filesystem::path> FindCounterpart(const std::filesystem::path& path);

} // namespace ned::editor::headersource

#endif // NED_EDITOR_HEADERSOURCE_H
