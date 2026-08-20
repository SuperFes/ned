//
// Trust registry for project-local `.ned/init.janet` files -- session-
// persistence slice 3.
//
// Loading a project's own Janet on open is arbitrary code execution
// triggered by cloning a repo -- the same class of concern ROADMAP.md
// records against Org Babel -- so nothing is ever loaded silently: an
// untrusted (or changed, or expired) init file gets a y/n/a prompt through
// the focused pane (the deferred-binary-open pattern), and only an
// explicit "always" lands here.
//
// Entries are keyed by the init file's normalized path and carry the
// file's content hash (a changed file always re-prompts, whatever its
// trust age) plus trustedAt/lastUsed stamps. Expiry keys off *lastUsed*,
// deliberately not trustedAt -- the user's own security framing: trust
// should decay with disuse (a stale clone whose init you no longer
// remember approving), not on a fixed timer that nags on daily-use
// projects. Every successful load refreshes lastUsed; pruning happens at
// store load time, dropping entries unaccessed past the expiry window and
// entries whose file no longer exists on disk.
//
// Same layering as Session.h/ProjectSession.h: a pure, unit-testable
// ProjectTrustStore plus mutex-guarded process-wide accessors.
//

#ifndef NED_EDITOR_PROJECTTRUST_H
#define NED_EDITOR_PROJECTTRUST_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace ned::editor {

// What the user chose at the trust prompt. Defined here (not in the UI
// layer) so the callback signature crossing main.cpp -> WindowManager ->
// BufferView stays an editor-layer concept.
enum class ProjectInitDecision {
    LoadOnce,   // y -- load now, remember nothing
    LoadAlways, // a -- load now and record trust for the current content hash
    Decline,    // n / C-g / Escape
};

struct ProjectTrustEntry {
    std::string  contentHash;
    std::int64_t trustedAt = 0; // Unix seconds
    std::int64_t lastUsed  = 0; // Unix seconds; refreshed on every successful load

    bool operator==(const ProjectTrustEntry&) const = default;
};

class ProjectTrustStore {
  public:
    // True only for an entry whose recorded hash matches -- an entry with a
    // stale hash is not trusted (the file changed), though it's left in
    // place so an "always" re-approval just overwrites it.
    [[nodiscard]] bool IsTrusted(const std::filesystem::path& initFile, std::string_view contentHash) const;

    void Trust(const std::filesystem::path& initFile, std::string contentHash,
               std::optional<std::int64_t> nowSeconds = std::nullopt);
    void Touch(const std::filesystem::path& initFile, std::optional<std::int64_t> nowSeconds = std::nullopt);

    // Drops entries whose lastUsed is more than expiryDays days before
    // nowSeconds. expiryDays <= 0 means never expire.
    void PruneExpired(std::int64_t nowSeconds, int expiryDays);
    // Drops entries whose init file no longer exists on disk -- dead clones
    // don't linger in the list.
    void PruneMissingFiles();

    // FilePlaceStore's exact file contracts: missing/malformed loads as
    // empty, save is tmp+rename creating parents, throwing on I/O failure.
    void LoadFromFile(const std::filesystem::path& path);
    void SaveToFile(const std::filesystem::path& path) const;

    [[nodiscard]] std::string ToJson() const;
    static ProjectTrustStore  FromJson(std::string_view json);
    [[nodiscard]] std::size_t Count() const;

    [[nodiscard]] std::optional<ProjectTrustEntry> Lookup(const std::filesystem::path& initFile) const;

  private:
    std::map<std::string, ProjectTrustEntry> entries_; // normalized init-file path -> entry
};

// FNV-1a 64 hex over the file's bytes; nullopt if unreadable. Content
// identity only -- this is change detection for the re-prompt rule, not a
// cryptographic commitment.
[[nodiscard]] std::optional<std::string> HashFileContent(const std::filesystem::path& path);

// -- Process-wide store + settings (mutex-guarded static state) ---------------

// Days of *disuse* before a trust entry expires; configured from Janet via
// ned/set-project-trust-expiry-days. Default 30; <= 0 means never expire.
void              SetProjectTrustExpiryDays(int days);
[[nodiscard]] int ProjectTrustExpiryDays();

// $XDG_STATE_HOME/ned/trusted.json (FilePlacesPath's sibling).
[[nodiscard]] std::filesystem::path TrustedFilePath();

// Load (+ prune expired and missing-file entries, per the header comment)
// and save the process-wide store at TrustedFilePath(). Both swallow every
// failure, same unattended contract as LoadFilePlaces/SaveFilePlaces.
// LoadProjectTrust must run after init.janet so a configured expiry window
// governs the prune.
void LoadProjectTrust();
void SaveProjectTrust();

[[nodiscard]] bool IsProjectInitTrusted(const std::filesystem::path& initFile, std::string_view contentHash);
void               RecordProjectInitTrust(const std::filesystem::path& initFile, std::string contentHash);
void               TouchProjectTrust(const std::filesystem::path& initFile);

// Tests only: empty store, default expiry.
void ResetProjectTrustForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTTRUST_H
