//
// A registry of named, saved projects -- the "storing projects" half of
// Named Projects & Multi-Project Sidebar (see ROADMAP.md): a small, disk-
// backed catalog letting a project be opened once, given a name, and
// switched back to later without retyping or remembering its path.
//
// Distinct from ProjectSession.h: that file is the per-project session
// state (open files, window layout, breakpoints) for one already-known
// root; this is the catalog of *which* roots are known at all, plus the
// display name attached to each. A registry entry's root is exactly what
// ProjectSessionPath() already hashes, so the two are additive, not
// overlapping.
//
// Same layering as ProjectTrust.h/Session.h: a pure, unit-testable
// ProjectRegistryStore plus mutex-guarded process-wide accessors.
//
// root is stored as a plain string, not a std::filesystem::path -- local
// paths today, future-proofed for a remote `user@host:/path` URI later
// (ROADMAP.md's Remote Development section) without a shape change here.
//
// No eager pruning of entries whose root no longer exists on disk, unlike
// ProjectTrustStore::PruneMissingFiles -- a stale *trust* entry is a
// security-relevant liability (silently trusting a since-replaced file at
// the same path), a stale *registry* entry is just clutter. A missing root
// is instead reported as an ordinary activation-time error when the user
// actually tries to switch to it.
//

#ifndef NED_EDITOR_PROJECTREGISTRY_H
#define NED_EDITOR_PROJECTREGISTRY_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

struct ProjectRegistryEntry {
    std::string  name;
    std::string  root;         // local path today; see header comment above
    std::int64_t lastUsed = 0; // Unix seconds; refreshed by Touch/Add

    bool operator==(const ProjectRegistryEntry&) const = default;
};

class ProjectRegistryStore {
  public:
    // Adds a new entry, or updates an existing one for the same root (name
    // refreshed, lastUsed bumped) -- keyed by the root's normalized path, so
    // re-registering an already-known root under any spelling never
    // duplicates. Returns true if this created a new entry, false if it
    // updated one that already existed.
    bool Add(std::string name, const std::filesystem::path& root, std::optional<std::int64_t> nowSeconds = std::nullopt);
    bool Remove(const std::filesystem::path& root);
    // False (no-op) if root isn't registered.
    bool Rename(const std::filesystem::path& root, std::string newName);
    void Touch(const std::filesystem::path& root, std::optional<std::int64_t> nowSeconds = std::nullopt);

    [[nodiscard]] std::optional<ProjectRegistryEntry> LookupByRoot(const std::filesystem::path& root) const;
    // Most-recently-used first -- the useful default order for a fuzzy
    // switch-project prompt before any typing narrows it.
    [[nodiscard]] std::vector<ProjectRegistryEntry> List() const;

    // FilePlaceStore's exact file contracts: missing/malformed loads as
    // empty, save is tmp+rename creating parents, throwing on I/O failure.
    void LoadFromFile(const std::filesystem::path& path);
    void SaveToFile(const std::filesystem::path& path) const;

    [[nodiscard]] std::string   ToJson() const;
    static ProjectRegistryStore FromJson(std::string_view json);
    [[nodiscard]] std::size_t   Count() const;

  private:
    std::map<std::string, ProjectRegistryEntry> entries_; // normalized root -> entry
};

// -- Process-wide store (mutex-guarded static state) --------------------------

// $XDG_STATE_HOME/ned/projects.json (TrustedFilePath's sibling).
[[nodiscard]] std::filesystem::path ProjectRegistryPath();

// Load and save the process-wide store. Both swallow every failure, same
// unattended contract as LoadFilePlaces/SaveFilePlaces/LoadProjectTrust.
void LoadProjectRegistry();
void SaveProjectRegistry();

// Each of these mutates the process-wide store and saves it immediately
// (small file, same "save on write" contract ProjectTrust's Trust/Touch
// use via RecordProjectInitTrust) -- see ProjectRegistryStore's own method
// docs above for return-value meaning.
bool RegisterProject(std::string name, const std::filesystem::path& root);
bool UnregisterProject(const std::filesystem::path& root);
bool RenameProject(const std::filesystem::path& root, std::string newName);
void TouchProject(const std::filesystem::path& root);

[[nodiscard]] std::vector<ProjectRegistryEntry>   ListProjects();
[[nodiscard]] std::optional<ProjectRegistryEntry> FindProjectByRoot(const std::filesystem::path& root);

// Tests only: empty store.
void ResetProjectRegistryForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTREGISTRY_H
