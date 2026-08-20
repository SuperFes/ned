//
// The VCS-agnostic vocabulary every version-control plugin implements
// (see ROADMAP.md's "External tool integration (version control and
// beyond)" entry). VcsProvider is a pure, Janet-free interface -- the
// Janet-specific adapter that actually calls into a plugin's callbacks
// lives in Source/Janet/JanetVcsProvider.h, keeping this header (and
// anything that only needs the *shape* of a provider, like
// VcsProviderRegistry/VcsRunner) free of any Janet dependency.
//
// v1 only implements the blame/log slice of the vocabulary (Detect,
// Blame*, Log*). Status/diff/stage-unstage/commit/branch are named below
// in a comment as reserved vocabulary a future plugin could implement --
// deliberately not given real methods yet, so this interface doesn't need
// a breaking rework once they land; see ROADMAP.md.
//

#ifndef NED_EDITOR_VCS_VCSPROVIDER_H
#define NED_EDITOR_VCS_VCSPROVIDER_H

#include <filesystem>
#include <string>
#include <vector>

namespace ned::editor::vcs {

// One line's worth of blame attribution -- returned per source line, in
// source-line order, by VcsProvider::ParseBlame. date is kept as whatever
// string the plugin's own parse produced (e.g. an ISO date) rather than a
// parsed C++ time type, since date formatting is inherently VCS/plugin
// specific; a caller wanting to interpolate color by age parses it once
// itself (see BufferView's blame-gutter cache).
struct VcsBlameLine {
    std::string commitHash;
    std::string author;
    std::string date;
    std::string summary;
};

// One commit's worth of log info, as returned by VcsProvider::ParseLog.
struct VcsLogEntry {
    std::string commitHash;
    std::string author;
    std::string date;
    std::string summary;
};

// An external command to run to satisfy one VCS operation -- argv[0] is
// the executable, resolved against $PATH the same way ChildProcess
// already resolves any other spawned command.
struct VcsCommandSpec {
    std::vector<std::string> argv;
};

// A VCS-agnostic provider: translates the common vocabulary below into
// whatever a specific VCS actually needs. Each operation is deliberately
// split into a "build the command" half and a "parse the output" half
// rather than one do-everything method -- see JanetVcsProvider.h's own
// header comment for why (a real Janet-threading constraint, not just
// factoring taste): a Janet-backed implementation can only ever run its
// callbacks on the main thread, so the actual process spawn/wait has to
// happen in between, on a background thread, without ever calling back
// into Janet from it.
class VcsProvider {
  public:
    virtual ~VcsProvider() = default;

    // True if this provider recognizes root as one of its own repositories
    // (e.g. a ".git" directory present). Used by VcsProviderRegistry to
    // pick which registered provider is active for a given project root.
    [[nodiscard]] virtual bool Detect(const std::filesystem::path& root) const = 0;

    [[nodiscard]] virtual VcsCommandSpec           BlameArgv(const std::filesystem::path& path) const = 0;
    [[nodiscard]] virtual std::vector<VcsBlameLine> ParseBlame(const std::string& stdout_) const       = 0;

    [[nodiscard]] virtual VcsCommandSpec          LogArgv(const std::filesystem::path& path) const = 0;
    [[nodiscard]] virtual std::vector<VcsLogEntry> ParseLog(const std::string& stdout_) const       = 0;

    // Reserved vocabulary, not implemented in v1 -- status, diff,
    // stage/unstage a hunk, commit, branch. A future provider
    // implementation adds real virtual methods for these; nothing above
    // needs to change to accommodate them.
};

} // namespace ned::editor::vcs

#endif // NED_EDITOR_VCS_VCSPROVIDER_H
