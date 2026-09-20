//
// The merged Actions/Files/Buffers candidate pool behind the
// search-everywhere popup (a command by name, a named keyboard macro
// (MacroRegistry.h) by name, a project file by relative path, an open
// buffer by name). Pure and UI-free -- FuzzyMatch.h's own convention -- and
// deliberately does not reuse FuzzyFilterAndRank: that function takes and
// returns bare std::string and re-sorts ties alphabetically, discarding
// which source a candidate came from and any secondary text (a file's
// path, a command's doc string) worth showing beside it. This calls
// FuzzyScore directly per candidate and keeps that provenance through its
// own sort instead.
//

#ifndef NED_EDITOR_SEARCHEVERYWHERE_H
#define NED_EDITOR_SEARCHEVERYWHERE_H

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

enum class SearchEverywhereKind {
    Command,
    Macro,
    File,
    Buffer,
    // search-everywhere-symbols-and-text follow-up: Symbol covers both the
    // current buffer's own symbols (gathered synchronously once per
    // session, Mode::symbolKind) and project-wide LSP workspace/symbol
    // results (arriving async, appended as they come back) -- distinguished
    // by which of SearchEverywhereCandidate's two location fields is set,
    // not by a separate kind, matching how JetBrains' own "Symbols" tab
    // doesn't separate local from project-wide either.
    Symbol,
    TextMatch,
    // search-everywhere-more-sources follow-up. Appended rather than slotted
    // in beside Command, because this enum's declaration order is also
    // RankSearchEverywhere's tie-break and Tab's cycle order -- the three
    // original categories stay where a user's fingers already expect them.
    //
    // ServerCommand is a running language server's own advertised
    // executeCommandProvider.commands (rust-analyzer.reloadWorkspace and
    // friends); Theme and Project are the two registries a user already
    // reaches for by name (UI/ThemeRegistry.h, Editor/Project/Registry.h).
    ServerCommand,
    Theme,
    Project,
};

// LSP-agnostic on purpose -- this module stays as dependency-free as
// FuzzyMatch.h/LocalScopes.h; BufferView converts to/from
// editor::lsp::Manager::ResolvedLocation at the one call site that needs it.
struct SearchEverywhereLocation {
    std::filesystem::path path;
    std::size_t           line;      // 0-indexed, LSP convention
    std::size_t           character; // 0-indexed
};

struct SearchEverywhereCandidate {
    SearchEverywhereKind kind;
    std::string          label;  // fuzzy-matched against
    std::string          detail; // shown dimmed alongside label: a doc string, a relative path, or ""

    // The chord that already runs this row, formatted (FormatKeySequence),
    // or "" for a row with no binding. Set for Command rows only -- a
    // palette that shows the binding beside the command is what teaches the
    // chord rather than replacing it, which is the whole point of listing
    // commands here at all. Display only: never fuzzy-matched against, so
    // typing "C-x" narrows to commands *named* that, not bound to it.
    std::string binding;

    // An opaque routing token the UI hands back on commit, never
    // interpreted here: the LSP serverKey that advertised a ServerCommand,
    // the root path of a Project. Empty for every other kind. A dedicated
    // field rather than reading it back out of `detail`, so prettifying
    // what a row displays can never break what committing it does.
    std::string target;

    // Exactly one of these is set for a Symbol candidate (which jump shape
    // applies), and remoteLocation is always set for a TextMatch one; both
    // stay unset for Command/Macro/File/Buffer.
    std::optional<std::size_t>              localByteOffset; // jump within the current buffer
    std::optional<SearchEverywhereLocation> remoteLocation;  // jump to another file
};

struct SearchEverywhereResult {
    std::size_t candidateIndex; // index into the candidates vector RankSearchEverywhere was called with
    int         score;
};

// FuzzyScore per candidate's label, filtered to kindFilter when set (nullopt
// means "All"). Sorted score descending; ties break first by kind (in
// SearchEverywhereKind's own declaration order: Command, Macro, File,
// Buffer, Symbol, TextMatch -- keeps same-scored rows grouped by kind rather
// than interleaved), then by label alphabetically. An empty query matches
// every candidate with score 0, same as FuzzyScore's own convention -- "All"
// with an empty query lists everything, grouped by kind.
[[nodiscard]] std::vector<SearchEverywhereResult> RankSearchEverywhere(
    const std::vector<SearchEverywhereCandidate>& candidates, std::string_view query,
    std::optional<SearchEverywhereKind> kindFilter);

} // namespace ned::editor

#endif // NED_EDITOR_SEARCHEVERYWHERE_H
