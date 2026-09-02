//
// The VCS-agnostic vocabulary every version-control plugin implements
// (see ROADMAP.md's "External tool integration (version control and
// beyond)" entry). VcsProvider is a pure, Janet-free interface -- the
// Janet-specific adapter that actually calls into a plugin's callbacks
// lives in Source/Janet/JanetVcsProvider.h, keeping this header (and
// anything that only needs the *shape* of a provider, like
// VcsProviderRegistry/VcsRunner) free of any Janet dependency.
//
// The vocabulary covers blame/log/diff (v1) plus status/stage-unstage/
// commit/branch (vocabulary-completion follow-up). The newer operations
// default to throwing "not supported by this provider" rather than being
// pure virtual -- a plugin registering only the blame/log/diff callbacks
// stays a valid provider, with the unimplemented operations degrading to
// a clear status-line error through VcsRunner's existing onError path
// instead of failing registration outright.
//

#ifndef NED_EDITOR_VCS_VCSPROVIDER_H
#define NED_EDITOR_VCS_VCSPROVIDER_H

#include <cstddef>
#include <filesystem>
#include <stdexcept>
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

// One changed region, as returned by VcsProvider::ParseDiff -- the same
// shape a unified diff's own "@@ -oldStart,oldCount +newStart,newCount @@"
// hunk header carries (1-indexed, matching git's own convention; a count
// of 0 means "no lines on this side," e.g. oldCount == 0 for a hunk that's
// pure insertion). Deliberately just the header fields, not the hunk's
// actual +/- line bodies -- a gutter marker only needs to know *which*
// buffer lines changed and how, not the old content, so there's nothing
// else worth asking a plugin to parse out.
struct VcsDiffHunk {
    std::size_t oldStart;
    std::size_t oldCount;
    std::size_t newStart;
    std::size_t newCount;
};

// One changed/untracked file, as returned by VcsProvider::ParseStatus.
// state is the VCS's own short status code kept verbatim (e.g. git
// porcelain's two-letter "XY" column -- " M", "A ", "??"), the same
// "don't reinterpret VCS-specific text in C++" call VcsBlameLine::date
// already made; path is relative to the repository root, exactly as the
// VCS reported it (the new name for a rename).
struct VcsStatusEntry {
    std::string state;
    std::string path;
};

// One branch, as returned by VcsProvider::ParseBranchList. current marks
// the currently-checked-out branch (at most one entry).
struct VcsBranchEntry {
    std::string name;
    bool        current;
};

// VCS side panel follow-up: one stash entry, as returned by
// VcsProvider::ParseStashList. ref is the VCS's own addressable form (git's
// "stash@{0}"), kept verbatim like every other *Entry ref/state field in
// this header; message is whatever summary text the VCS attaches (git's own
// "WIP on <branch>: <subject>" by default, or a custom message if one was
// given to StashPushArgv).
struct VcsStashEntry {
    std::string ref;
    std::string message;
};

// VCS side panel follow-up: ahead/behind counts relative to the current
// branch's upstream, as returned by VcsProvider::ParseAheadBehind. Both 0
// means up to date; a provider/repo with no upstream configured reports
// this by throwing from AheadBehindArgv/failing the spawned process (the
// same "not supported"/failed-command surface every other operation here
// uses), not via a special "no upstream" state in this struct.
struct VcsAheadBehind {
    std::size_t ahead;
    std::size_t behind;
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

    // Blame/log/diff were pure virtual until the vocabulary-completion
    // follow-up made every operation but Detect optional (the Janet
    // registration takes a table of callbacks with only :detect required)
    // -- now default-throwing like the newer operations below, so a
    // partial provider is one consistent concept rather than two.
    [[nodiscard]] virtual VcsCommandSpec BlameArgv(const std::filesystem::path& path) const {
        (void)path;
        throw std::runtime_error("blame not supported by this provider");
    }
    [[nodiscard]] virtual std::vector<VcsBlameLine> ParseBlame(const std::string& stdout_) const {
        (void)stdout_;
        throw std::runtime_error("blame not supported by this provider");
    }

    [[nodiscard]] virtual VcsCommandSpec LogArgv(const std::filesystem::path& path) const {
        (void)path;
        throw std::runtime_error("log not supported by this provider");
    }
    [[nodiscard]] virtual std::vector<VcsLogEntry> ParseLog(const std::string& stdout_) const {
        (void)stdout_;
        throw std::runtime_error("log not supported by this provider");
    }

    // Diff gutter follow-up: path's changes against whatever this provider
    // considers "the comparison point" (HEAD for git) -- feeds BufferView's
    // live-refreshing added/modified/removed gutter markers.
    [[nodiscard]] virtual VcsCommandSpec DiffArgv(const std::filesystem::path& path) const {
        (void)path;
        throw std::runtime_error("diff not supported by this provider");
    }
    [[nodiscard]] virtual std::vector<VcsDiffHunk> ParseDiff(const std::string& stdout_) const {
        (void)stdout_;
        throw std::runtime_error("diff not supported by this provider");
    }

    // Multibuffers follow-up: the whole working tree's changed content
    // against root, with real (non-zero) context lines -- unlike DiffArgv
    // above, which is deliberately -U0/single-file for the gutter's own
    // "which lines changed" needs, this is meant to be read, not just
    // measured, so it's root-scoped (every changed file, not one) and
    // keeps a provider's normal default context. No parse half: the raw
    // diff text is consumed directly by Vcs/DiffPatch.h's ParseDiffHunks,
    // the same "verbatim, never reconstructed" posture ExtractHunkPatch
    // already takes toward diff output.
    [[nodiscard]] virtual VcsCommandSpec WorkingDiffArgv(const std::filesystem::path& root) const {
        (void)root;
        throw std::runtime_error("full diff not supported by this provider");
    }

    // Everything below is the vocabulary-completion follow-up's optional
    // half: default-throwing rather than pure virtual (see this header's
    // own top comment). Operations without a parse counterpart
    // (stage/unstage/commit/branch-switch/branch-create) report success by
    // exit code alone -- the subprocess's own output is only ever used as
    // failure detail, nothing worth asking a plugin to parse out of it.

    // The working tree's changed/untracked files, relative to root.
    [[nodiscard]] virtual VcsCommandSpec StatusArgv(const std::filesystem::path& root) const {
        (void)root;
        throw std::runtime_error("status not supported by this provider");
    }
    [[nodiscard]] virtual std::vector<VcsStatusEntry> ParseStatus(const std::string& stdout_) const {
        (void)stdout_;
        throw std::runtime_error("status not supported by this provider");
    }

    // Stage/unstage one whole file. Hunk-level staging is deliberately not
    // in this vocabulary yet -- it needs a hunk-bodies-carrying diff parse
    // plus patch construction/application, its own follow-up slice (see
    // ROADMAP.md), not two more argv builders here.
    [[nodiscard]] virtual VcsCommandSpec StageArgv(const std::filesystem::path& path) const {
        (void)path;
        throw std::runtime_error("stage not supported by this provider");
    }
    [[nodiscard]] virtual VcsCommandSpec UnstageArgv(const std::filesystem::path& path) const {
        (void)path;
        throw std::runtime_error("unstage not supported by this provider");
    }

    // Hunk-level staging (the follow-up slice the whole-file pair above
    // originally deferred). StagedDiffArgv is DiffArgv's index-vs-
    // comparison-point counterpart (`git diff --cached` for git) -- the
    // diff an *unstage* selects its hunk from, since the hunk to pull back
    // out of the index by definition isn't in the worktree diff. Stage/
    // UnstagePatchArgv apply a patch file (written by VcsRunner from
    // DiffPatch.h's verbatim hunk slice) to the staging area, forward and
    // reverse respectively; no parse halves anywhere here -- the raw diff
    // output is consumed by ExtractHunkPatch in C++, and patch application
    // succeeds on exit code 0 alone.
    [[nodiscard]] virtual VcsCommandSpec StagedDiffArgv(const std::filesystem::path& path) const {
        (void)path;
        throw std::runtime_error("staged diff not supported by this provider");
    }
    [[nodiscard]] virtual VcsCommandSpec StagePatchArgv(const std::filesystem::path& root,
                                                        const std::filesystem::path& patchPath) const {
        (void)root;
        (void)patchPath;
        throw std::runtime_error("hunk staging not supported by this provider");
    }
    [[nodiscard]] virtual VcsCommandSpec UnstagePatchArgv(const std::filesystem::path& root,
                                                          const std::filesystem::path& patchPath) const {
        (void)root;
        (void)patchPath;
        throw std::runtime_error("hunk unstaging not supported by this provider");
    }

    // Commit whatever is currently staged. message may be multi-line
    // (multi-line-commit-message follow-up: composed in a real buffer, see
    // BufferView::BeginVcsCommitMessage in Source/UI/BufferView.cpp) --
    // passed straight through argv (no shell involved, see
    // Process/ChildProcess.h), so an embedded newline needs no special
    // handling here.
    [[nodiscard]] virtual VcsCommandSpec CommitArgv(const std::filesystem::path& root, const std::string& message) const {
        (void)root;
        (void)message;
        throw std::runtime_error("commit not supported by this provider");
    }

    [[nodiscard]] virtual VcsCommandSpec BranchListArgv(const std::filesystem::path& root) const {
        (void)root;
        throw std::runtime_error("branch listing not supported by this provider");
    }
    [[nodiscard]] virtual std::vector<VcsBranchEntry> ParseBranchList(const std::string& stdout_) const {
        (void)stdout_;
        throw std::runtime_error("branch listing not supported by this provider");
    }

    [[nodiscard]] virtual VcsCommandSpec BranchSwitchArgv(const std::filesystem::path& root, const std::string& name) const {
        (void)root;
        (void)name;
        throw std::runtime_error("branch switching not supported by this provider");
    }
    [[nodiscard]] virtual VcsCommandSpec BranchCreateArgv(const std::filesystem::path& root, const std::string& name) const {
        (void)root;
        (void)name;
        throw std::runtime_error("branch creation not supported by this provider");
    }

    // VCS side panel follow-up: discard/revert/stash/push-pull-fetch/
    // ahead-behind, the vocabulary the panel's remaining ROADMAP items need.
    // Same default-throwing, argv-builder(+optional parse-half) shape as
    // every operation above.

    // Reverts path's own working-tree changes back to HEAD (distinct from
    // UnstageArgv, which only moves the index) -- exit code only, no parse
    // half. The one destructive operation in this vocabulary; the caller
    // (VcsPanel) is expected to confirm before calling VcsRunner::
    // RequestRevert, not this method itself.
    [[nodiscard]] virtual VcsCommandSpec RevertArgv(const std::filesystem::path& path) const {
        (void)path;
        throw std::runtime_error("revert not supported by this provider");
    }

    [[nodiscard]] virtual VcsCommandSpec StashListArgv(const std::filesystem::path& root) const {
        (void)root;
        throw std::runtime_error("stash listing not supported by this provider");
    }
    [[nodiscard]] virtual std::vector<VcsStashEntry> ParseStashList(const std::string& stdout_) const {
        (void)stdout_;
        throw std::runtime_error("stash listing not supported by this provider");
    }
    // message may be empty -- an empty message means "use the VCS's own
    // default stash message" (git: "WIP on <branch>: ..."), not a request to
    // pass a literal empty string through to the underlying command.
    [[nodiscard]] virtual VcsCommandSpec StashPushArgv(const std::filesystem::path& root, const std::string& message) const {
        (void)root;
        (void)message;
        throw std::runtime_error("stash push not supported by this provider");
    }
    [[nodiscard]] virtual VcsCommandSpec StashPopArgv(const std::filesystem::path& root, const std::string& stashRef) const {
        (void)root;
        (void)stashRef;
        throw std::runtime_error("stash pop not supported by this provider");
    }
    [[nodiscard]] virtual VcsCommandSpec StashDropArgv(const std::filesystem::path& root, const std::string& stashRef) const {
        (void)root;
        (void)stashRef;
        throw std::runtime_error("stash drop not supported by this provider");
    }

    // Push/pull/fetch rely on an already-configured upstream tracking
    // branch -- no --set-upstream/remote-selection vocabulary here (a
    // documented v1 cut, see ROADMAP.md); a repo with no upstream just
    // fails via the same exit-code/stderr surface every other operation
    // reports failure through.
    [[nodiscard]] virtual VcsCommandSpec PushArgv(const std::filesystem::path& root) const {
        (void)root;
        throw std::runtime_error("push not supported by this provider");
    }
    [[nodiscard]] virtual VcsCommandSpec PullArgv(const std::filesystem::path& root) const {
        (void)root;
        throw std::runtime_error("pull not supported by this provider");
    }
    [[nodiscard]] virtual VcsCommandSpec FetchArgv(const std::filesystem::path& root) const {
        (void)root;
        throw std::runtime_error("fetch not supported by this provider");
    }

    [[nodiscard]] virtual VcsCommandSpec AheadBehindArgv(const std::filesystem::path& root) const {
        (void)root;
        throw std::runtime_error("ahead/behind not supported by this provider");
    }
    [[nodiscard]] virtual VcsAheadBehind ParseAheadBehind(const std::string& stdout_) const {
        (void)stdout_;
        throw std::runtime_error("ahead/behind not supported by this provider");
    }
};

} // namespace ned::editor::vcs

#endif // NED_EDITOR_VCS_VCSPROVIDER_H
