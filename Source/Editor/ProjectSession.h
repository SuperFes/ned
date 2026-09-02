//
// Per-project session persistence -- session-persistence slice 2, beside
// (not on top of) Session.h's per-file save-place.
//
// A "project session" is the restorable shape of a working session in one
// project root: which file buffers were open, which was active, the
// sidebar's visibility/width, the window-split layout, and the DAP
// breakpoint store (closing the "persisting breakpoints across restarts" v1
// cut recorded in ROADMAP.md's DAP entry). Still not persisted: anything
// derivable from the files themselves.
//
// Storage is contextual, per the user's own ask ("we should try to be
// smart about how and where we store session data"): a project with a
// `.ned/` directory at its root (strictly opt-in -- nothing here ever
// creates one) keeps its session in `<root>/.ned/session.json`, right
// beside the project; everything else goes to
// `$XDG_STATE_HOME/ned/sessions/<fnv1a64-of-root>.json` so uninvited
// dot-directories never appear in a repo. Only a *real* project -- one
// whose root carries a VCS marker or `.ned/` (HasProjectMarker) -- gets a
// session at all: a bare non-project cwd like $HOME must not accumulate
// one (see main.cpp's eligibility wiring).
//
// Same layering as Session.h: pure, unit-testable pieces
// (ProjectSessionData JSON round-trip, path/marker helpers) plus a small
// set of process-wide accessors (mutex-guarded static, the TabWidth.h/
// ProjectRoot.h pattern) holding the active root + enabled toggle, so
// WindowManager's capture path and main.cpp's restore path agree without
// new constructor plumbing.
//

#ifndef NED_EDITOR_PROJECTSESSION_H
#define NED_EDITOR_PROJECTSESSION_H

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

// One node of a captured window-split tree, stored flat (a vector, not a
// pointer tree) so ProjectSessionData keeps ordinary value semantics --
// default copy/equality both just work, no unique_ptr-vs-pointee-identity
// trap to hand-roll (WindowNode itself, the *live* runtime tree in
// Source/UI/WindowManager.h, is the pointer-based version this gets
// captured from and rebuilt into). first/second are indices into the same
// WindowLayoutNode vector, always < this node's own index -- the vector is
// built post-order (children appended before their parent), so the root is
// always the *last* element, and indices only ever point backward, which is
// also what keeps a malformed/corrupted forward-or-self-referencing index
// from recursing forever on restore.
struct WindowLayoutNode {
    enum class Kind { Leaf,
                      SplitBelow,
                      SplitRight };

    Kind                                 kind = Kind::Leaf;
    std::optional<std::filesystem::path> file;          // Leaf only, absolute
    std::optional<std::size_t>           first, second; // SplitBelow/SplitRight only
    // Split-resize follow-up: `first`'s fractional share of the split
    // (SplitBelow/SplitRight only) -- WindowManager::CaptureWindowLayout's
    // own WindowNode::ratio, persisted so a resized layout survives a
    // restart instead of snapping back to 50/50 every restore.
    float ratio = 0.5f;

    bool operator==(const WindowLayoutNode&) const = default;
};

struct ProjectSessionData {
    std::vector<std::filesystem::path>   openFiles; // absolute, in BufferList order
    std::optional<std::filesystem::path> activeFile;
    std::optional<bool>                  sidebarVisible;
    std::optional<int>                   sidebarWidth;
    // Normalized path key -> sorted 1-based lines, DapManager's own store
    // shape verbatim (see DapManager::AllBreakpoints/RestoreBreakpoints).
    std::map<std::string, std::vector<std::size_t>> breakpoints;

    // ACP auto-reconnect follow-up: the agent name passed to this project's
    // most recent AcpManager::StartSession, if any -- lets opening the chat
    // panel reconnect to whatever agent this project was last using instead
    // of requiring the "ACP agent:" prompt every time. nullopt until a
    // session has ever been started in this project. Not cleared by
    // StopSession -- deliberately sticky, mirroring AcpManager::AgentName()'s
    // own "empty before the first StartSession, never reset after" contract.
    std::optional<std::string> lastAcpAgent;

    // Empty means "no captured layout" (falls back to the pre-existing
    // single-pane restore) -- WindowManager::CaptureWindowLayout leaves it
    // empty rather than populate a partial tree when some leaf's buffer has
    // no path (a scratch buffer showing in a pane, say). See
    // WindowLayoutNode's own doc comment for the "root is the last element"
    // convention. focusedPanePath is 0 (first) / 1 (second) at each split
    // node walking down from the root to the leaf that had keyboard focus at
    // capture time; empty means the root is itself that leaf.
    std::vector<WindowLayoutNode> windowLayout;
    std::vector<int>              focusedPanePath;

    bool operator==(const ProjectSessionData&) const = default;
};

// JSON round-trip. FromJson returns nullopt for anything unparseable
// (malformed file -> no session, never a startup failure), tolerating
// individually malformed entries the same way FilePlaceStore::FromJson
// does.
[[nodiscard]] std::string                       ProjectSessionToJson(const ProjectSessionData&    data,
                                                                     const std::filesystem::path& root);
[[nodiscard]] std::optional<ProjectSessionData> ProjectSessionFromJson(std::string_view json);

// Whether dir itself carries a project marker: one of the VCS marker
// directories DetectProjectRoot already recognizes (.git/.hg/.svn/.bzr) or
// a `.ned/` directory.
[[nodiscard]] bool HasProjectMarker(const std::filesystem::path& dir);

// Walks upward from startDir (inclusive) for the nearest directory
// HasProjectMarker accepts -- the no-arg-launch counterpart to
// DetectProjectRoot's file-argument walk (which main.cpp still uses for an
// explicit path; this exists because the no-arg case previously just took
// cwd as the root outright, which can't distinguish "a project" from
// "some directory"). nullopt if the walk exhausts without a marker.
[[nodiscard]] std::optional<std::filesystem::path> FindProjectMarkerRoot(const std::filesystem::path& startDir);

// $XDG_STATE_HOME/ned/sessions (Session.h's FilePlacesPath sibling; same
// resolution, same throw-if-no-HOME contract).
[[nodiscard]] std::filesystem::path SessionsDirectory();

// Where root's session lives: `<root>/.ned/session.json` when `<root>/.ned`
// exists as a directory (the opt-in), else
// SessionsDirectory()/<fnv1a64-of-normalized-root>.json. Pure -- never
// creates anything.
[[nodiscard]] std::filesystem::path ProjectSessionPath(const std::filesystem::path& root);

// Pure file I/O: LoadProjectSessionFile returns nullopt for a missing or
// malformed file; SaveProjectSessionFile writes via .ned-tmp + rename
// (creating parent directories), throwing std::runtime_error on failure --
// FilePlaceStore::LoadFromFile/SaveToFile's exact contract.
[[nodiscard]] std::optional<ProjectSessionData> LoadProjectSessionFile(const std::filesystem::path& path);
void                                            SaveProjectSessionFile(const ProjectSessionData& data, const std::filesystem::path& root,
                                                                       const std::filesystem::path& path);

// -- Process-wide root + toggle (mutex-guarded static state) ------------------

// Configured from Janet via ned/set-session-restore; default on. Off
// disables restore AND capture/save, mirroring SetSavePlaceEnabled's
// symmetric contract.
void               SetSessionRestoreEnabled(bool enabled);
[[nodiscard]] bool SessionRestoreEnabled();

// The root whose session this run reads/writes -- set once by main.cpp,
// only when that root genuinely is a project (HasProjectMarker); never set
// for the bare-cwd fallback. nullopt (the default) makes
// LoadActiveProjectSession/SaveActiveProjectSession no-ops, which is what
// keeps every test and every non-project launch session-free without any
// caller-side guards.
void                                               SetActiveProjectSessionRoot(std::optional<std::filesystem::path> root);
[[nodiscard]] std::optional<std::filesystem::path> ActiveProjectSessionRoot();

// Load/save against ActiveProjectSessionRoot()'s ProjectSessionPath.
// Both no-ops (nullopt / silent return) when no root is set or the toggle
// is off; save additionally skips the disk write when data serializes
// identically to what this process last wrote (the ProjectSession
// counterpart of FilePlaceStore's Dirty() skip -- this runs on the same
// unattended 5s tick), and swallows I/O failures for the same
// nothing-to-report-to reason SaveFilePlaces documents.
[[nodiscard]] std::optional<ProjectSessionData> LoadActiveProjectSession();
void                                            SaveActiveProjectSession(const ProjectSessionData& data);

// Tests only: clears root, last-saved memo, and re-enables the toggle.
void ResetProjectSessionForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTSESSION_H
