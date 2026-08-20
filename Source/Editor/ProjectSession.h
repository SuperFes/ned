//
// Per-project session persistence -- session-persistence slice 2, beside
// (not on top of) Session.h's per-file save-place.
//
// A "project session" is the restorable shape of a working session in one
// project root: which file buffers were open, which was active, the
// sidebar's visibility/width, and the DAP breakpoint store (closing the
// "persisting breakpoints across restarts" v1 cut recorded in ROADMAP.md's
// DAP entry). Deliberately NOT persisted: window-split layout (an explicit
// slice-2 cut -- panes reference buffers by address and the split tree is
// real extra serialization surface for marginal value) and anything
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

struct ProjectSessionData {
    std::vector<std::filesystem::path>   openFiles; // absolute, in BufferList order
    std::optional<std::filesystem::path> activeFile;
    std::optional<bool>                  sidebarVisible;
    std::optional<int>                   sidebarWidth;
    // Normalized path key -> sorted 1-based lines, DapManager's own store
    // shape verbatim (see DapManager::AllBreakpoints/RestoreBreakpoints).
    std::map<std::string, std::vector<std::size_t>> breakpoints;

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
