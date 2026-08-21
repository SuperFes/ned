#include "Backup.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "ScratchPad.h"
#include "Session.h"

namespace ned::editor {

namespace {

    constexpr std::string_view kAutoSaveName     = "autosave";
    constexpr std::string_view kPathSidecarName  = "path";
    constexpr std::string_view kVersionExtension = ".bak";

    // Buffers/files past this size are skipped by both writers: the timer
    // tick runs on the event-loop thread, and copying out a multi-hundred-MiB
    // file every few seconds would stall the UI. Not yet Janet-configurable
    // -- a knob can follow if anyone actually hits it.
    constexpr std::uintmax_t kMaxBackupBytes = 64ull * 1024 * 1024;

    constexpr std::int64_t kPruneIntervalSeconds = 3600;

    // Duplicated from ProjectSession.cpp's private copy, the same "not worth
    // a new dependency for something this small" call kProjectMarkers itself
    // made -- the two must keep hashing identically only in the sense that
    // each is a correct FNV-1a 64, not that they share state.
    std::string Fnv1a64Hex(std::string_view key) {
        std::uint64_t hash = 1469598103934665603ull;
        for (const unsigned char byte : key) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        char buffer[17];
        std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
        return buffer;
    }

    std::mutex& BackupMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& AutoSaveEnabledStorage() {
        static bool enabled = true;
        return enabled;
    }

    int& MaxAgeDaysStorage() {
        static int days = 14;
        return days;
    }

    int& MaxVersionsStorage() {
        static int versions = 20;
        return versions;
    }

    // NormalizePathKey string -> the ContentGeneration() last written as that
    // file's autosave -- the dirty-skip memo AutoSaveFileBuffers compares
    // against. Bounded by files opened per process; entries are erased by
    // RemoveAutoSave.
    std::unordered_map<std::string, std::size_t>& AutoSaveGenerationStorage() {
        static std::unordered_map<std::string, std::size_t> generations;
        return generations;
    }

    std::optional<std::int64_t>& LastPruneStorage() {
        static std::optional<std::int64_t> lastPrune;
        return lastPrune;
    }

    std::int64_t NowOr(std::optional<std::int64_t> nowSeconds) {
        if (nowSeconds) {
            return *nowSeconds;
        }
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    // The sibling-temp-file-then-rename idiom Buffer::SaveToFile and the
    // session stores each carry their own private copy of.
    void AtomicWrite(const std::filesystem::path& path, std::string_view content) {
        const std::filesystem::path temporary = path.string() + ".ned-tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) {
                throw std::runtime_error("ned: cannot write backup file \"" + temporary.string() + "\"");
            }
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!file.flush()) {
                std::error_code ec;
                std::filesystem::remove(temporary, ec);
                throw std::runtime_error("ned: failed writing backup file \"" + temporary.string() + "\"");
            }
        }
        std::filesystem::rename(temporary, path);
    }

    // Creates file's backup directory and records its normalized original
    // path in the `path` sidecar (once -- the hash-named directory is
    // otherwise unmappable back to the file it belongs to).
    std::filesystem::path EnsureBackupDirectory(const std::filesystem::path& file) {
        const std::filesystem::path directory = BackupDirectoryForFile(file);
        std::filesystem::create_directories(directory);
        const std::filesystem::path sidecar = directory / kPathSidecarName;
        if (!std::filesystem::exists(sidecar)) {
            AtomicWrite(sidecar, FilePlaceStore::NormalizePathKey(file) + "\n");
        }
        return directory;
    }

    // v-<UTC %Y%m%d-%H%M%S>-<seq>.bak: fixed-width fields plus an
    // always-present two-digit sequence, so lexicographic filename order is
    // chronological order (a variable-width or sometimes-absent suffix would
    // break that).
    std::string VersionFileName(std::int64_t timestampSeconds, int sequence) {
        const std::time_t seconds = static_cast<std::time_t>(timestampSeconds);
        std::tm           utc{};
        gmtime_r(&seconds, &utc);
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "v-%04d%02d%02d-%02d%02d%02d-%02d", utc.tm_year + 1900, utc.tm_mon + 1,
                      utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec, sequence);
        return std::string(buffer) + std::string(kVersionExtension);
    }

    // First not-yet-taken sequence slot for this second, probing -00..-99; a
    // process somehow saving the same file more than 100 times in one second
    // overwrites -99 rather than growing a wider field (documented, not
    // defended).
    std::filesystem::path NextVersionPath(const std::filesystem::path& directory, std::int64_t timestampSeconds) {
        for (int sequence = 0; sequence < 100; ++sequence) {
            const std::filesystem::path candidate = directory / VersionFileName(timestampSeconds, sequence);
            if (!std::filesystem::exists(candidate)) {
                return candidate;
            }
        }
        return directory / VersionFileName(timestampSeconds, 99);
    }

    // Inverse of VersionFileName: Unix seconds from a "v-...-NN.bak" name,
    // std::nullopt for anything else in the directory (the sidecar, the
    // autosave, a stray .ned-tmp).
    std::optional<std::int64_t> ParseVersionTimestamp(const std::string& filename) {
        int year     = 0;
        int month    = 0;
        int day      = 0;
        int hour     = 0;
        int minute   = 0;
        int second   = 0;
        int sequence = 0;
        if (std::sscanf(filename.c_str(), "v-%4d%2d%2d-%2d%2d%2d-%2d.bak", &year, &month, &day, &hour, &minute, &second,
                        &sequence) != 7) {
            return std::nullopt;
        }
        std::tm utc{};
        utc.tm_year = year - 1900;
        utc.tm_mon  = month - 1;
        utc.tm_mday = day;
        utc.tm_hour = hour;
        utc.tm_min  = minute;
        utc.tm_sec  = second;
        return static_cast<std::int64_t>(timegm(&utc));
    }

    std::string LocalTimeLabel(std::int64_t timestampSeconds) {
        const std::time_t seconds = static_cast<std::time_t>(timestampSeconds);
        std::tm           local{};
        localtime_r(&seconds, &local);
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", local.tm_year + 1900, local.tm_mon + 1,
                      local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
        return buffer;
    }

    std::optional<std::int64_t> FileMtimeSeconds(const std::filesystem::path& path) {
        std::error_code                       ec;
        const std::filesystem::file_time_type mtime = std::filesystem::last_write_time(path, ec);
        if (ec) {
            return std::nullopt;
        }
        const auto systemTime = std::chrono::clock_cast<std::chrono::system_clock>(mtime);
        return std::chrono::duration_cast<std::chrono::seconds>(systemTime.time_since_epoch()).count();
    }

    bool IsScratchFile(const std::filesystem::path& file) {
        try {
            return std::filesystem::weakly_canonical(file.parent_path()) == std::filesystem::weakly_canonical(ScratchDirectory());
        }
        catch (const std::exception&) {
            // Can't resolve the scratch directory at all -> file can't be in
            // it. Deciding "not a scratch" here (rather than letting the
            // throw abort a whole backup/autosave) keeps this check a pure
            // filter, not a new failure mode.
            return false;
        }
    }

    void PruneOneDirectory(const std::filesystem::path& directory, std::int64_t nowSeconds, int maxAgeDays,
                           int maxVersions) {
        struct Version {
            std::filesystem::path path;
            std::int64_t          timestampSeconds;
        };
        std::vector<Version> versions;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (const std::optional<std::int64_t> timestamp = ParseVersionTimestamp(entry.path().filename().string())) {
                versions.push_back({entry.path(), *timestamp});
            }
        }

        std::error_code ec;
        if (maxAgeDays > 0) {
            const std::int64_t cutoff = nowSeconds - static_cast<std::int64_t>(maxAgeDays) * 24 * 60 * 60;
            std::erase_if(versions, [&](const Version& version) {
                if (version.timestampSeconds < cutoff) {
                    std::filesystem::remove(version.path, ec);
                    return true;
                }
                return false;
            });

            // An autosave this old is an orphan: whatever editing session
            // wrote it never came back to save or recover.
            const std::filesystem::path autosave = directory / kAutoSaveName;
            if (const std::optional<std::int64_t> mtime = FileMtimeSeconds(autosave); mtime && *mtime < cutoff) {
                std::filesystem::remove(autosave, ec);
            }
        }

        if (maxVersions > 0 && versions.size() > static_cast<std::size_t>(maxVersions)) {
            std::sort(versions.begin(), versions.end(),
                      [](const Version& a, const Version& b) { return a.path.filename() > b.path.filename(); });
            for (std::size_t index = static_cast<std::size_t>(maxVersions); index < versions.size(); ++index) {
                std::filesystem::remove(versions[index].path, ec);
            }
        }

        // A directory left holding only its `path` sidecar (or nothing) has
        // no snapshot left to recover -- drop it entirely.
        bool onlySidecar = true;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().filename() != kPathSidecarName) {
                onlySidecar = false;
                break;
            }
        }
        if (onlySidecar) {
            std::filesystem::remove_all(directory, ec);
        }
    }

} // namespace

std::filesystem::path BackupsDirectory() {
    if (const char* xdgStateHome = std::getenv("XDG_STATE_HOME"); xdgStateHome && *xdgStateHome) {
        return std::filesystem::path(xdgStateHome) / "ned" / "backups";
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "state" / "ned" / "backups";
    }

    throw std::runtime_error("ned: cannot determine state directory (neither XDG_STATE_HOME nor HOME is set)");
}

std::filesystem::path BackupDirectoryForFile(const std::filesystem::path& file) {
    return BackupsDirectory() / Fnv1a64Hex(FilePlaceStore::NormalizePathKey(file));
}

std::vector<BackupVersion> ListBackupVersions(const std::filesystem::path& file) {
    std::vector<BackupVersion> versions;

    try {
        const std::filesystem::path directory = BackupDirectoryForFile(file);

        const std::filesystem::path autosave = directory / kAutoSaveName;
        if (const std::optional<std::int64_t> mtime = FileMtimeSeconds(autosave)) {
            versions.push_back({autosave, "autosave (crash recovery)", *mtime, true});
        }

        std::vector<BackupVersion> backups;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (const std::optional<std::int64_t> timestamp = ParseVersionTimestamp(entry.path().filename().string())) {
                backups.push_back({entry.path(), LocalTimeLabel(*timestamp), *timestamp, false});
            }
        }
        // Filename order is chronological by construction (see
        // VersionFileName), so newest-first is a descending filename sort.
        std::sort(backups.begin(), backups.end(),
                  [](const BackupVersion& a, const BackupVersion& b) { return a.path.filename() > b.path.filename(); });
        versions.insert(versions.end(), backups.begin(), backups.end());
    }
    catch (const std::exception&) {
        return {}; // no backup directory yet, unreadable, unset $HOME -- nothing to recover
    }

    return versions;
}

std::string ReadBackupVersion(const std::filesystem::path& versionPath) {
    std::ifstream file(versionPath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("ned: cannot read backup \"" + versionPath.string() + "\"");
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) {
        throw std::runtime_error("ned: error reading backup \"" + versionPath.string() + "\"");
    }
    return content;
}

void BackupFileBeforeSave(const std::filesystem::path& file, std::optional<std::int64_t> nowSeconds) {
    try {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec)) {
            return; // first save of a new file -- no prior content to preserve
        }
        if (IsScratchFile(file)) {
            return;
        }
        const std::uintmax_t size = std::filesystem::file_size(file, ec);
        if (ec || size > kMaxBackupBytes) {
            return;
        }

        const std::filesystem::path directory = EnsureBackupDirectory(file);
        std::filesystem::copy_file(file, NextVersionPath(directory, NowOr(nowSeconds)),
                                   std::filesystem::copy_options::overwrite_existing);
    }
    catch (const std::exception&) {
        // Swallowed -- a failed backup must never block the save it precedes.
    }
}

void WriteAutoSave(const std::filesystem::path& file, std::string_view content) {
    AtomicWrite(EnsureBackupDirectory(file) / kAutoSaveName, content);
}

void RemoveAutoSave(const std::filesystem::path& file) {
    try {
        std::error_code ec;
        std::filesystem::remove(BackupDirectoryForFile(file) / kAutoSaveName, ec);
        const std::lock_guard<std::mutex> lock(BackupMutex());
        AutoSaveGenerationStorage().erase(FilePlaceStore::NormalizePathKey(file));
    }
    catch (const std::exception&) {
        // Swallowed -- the save this rides on already succeeded.
    }
}

void AutoSaveFileBuffers(text::BufferList& bufferList) {
    if (!FileAutoSaveEnabled()) {
        return;
    }

    for (const auto& buffer : bufferList.Buffers()) {
        // No PreviewBuffer() check needed: only Modified() buffers are
        // snapshotted, and PreviewBuffer() self-clears (promotes) the moment
        // its buffer becomes Modified() -- a modified preview cannot exist.
        if (!buffer->Path().has_value() || buffer->IsLoading() || !buffer->Modified()) {
            continue;
        }
        try {
            if (IsScratchFile(*buffer->Path())) {
                continue; // AutoSaveScratchBuffers' territory
            }
            if (buffer->Content().ByteLength() > kMaxBackupBytes) {
                continue;
            }

            const std::string key        = FilePlaceStore::NormalizePathKey(*buffer->Path());
            const std::size_t generation = buffer->ContentGeneration();
            {
                const std::lock_guard<std::mutex> lock(BackupMutex());
                const auto                        memo = AutoSaveGenerationStorage().find(key);
                if (memo != AutoSaveGenerationStorage().end() && memo->second == generation) {
                    continue; // unchanged since the last snapshot
                }
            }

            WriteAutoSave(*buffer->Path(), buffer->Text());

            const std::lock_guard<std::mutex> lock(BackupMutex());
            AutoSaveGenerationStorage()[key] = generation;
        }
        catch (const std::exception&) {
            // Swallowed -- unattended timer, next tick retries.
        }
    }
}

void PruneBackups(std::optional<std::int64_t> nowSeconds) {
    const std::int64_t now         = NowOr(nowSeconds);
    const int          maxAgeDays  = BackupMaxAgeDays();
    const int          maxVersions = BackupMaxVersions();

    try {
        for (const auto& entry : std::filesystem::directory_iterator(BackupsDirectory())) {
            if (!entry.is_directory()) {
                continue;
            }
            try {
                PruneOneDirectory(entry.path(), now, maxAgeDays, maxVersions);
            }
            catch (const std::exception&) {
                // One unreadable directory shouldn't stop the rest.
            }
        }
    }
    catch (const std::exception&) {
        // No backups directory yet, or it can't be listed -- nothing to prune.
    }
}

void MaybePruneBackups(std::optional<std::int64_t> nowSeconds) {
    const std::int64_t now = NowOr(nowSeconds);
    {
        const std::lock_guard<std::mutex> lock(BackupMutex());
        std::optional<std::int64_t>&      lastPrune = LastPruneStorage();
        if (lastPrune && now - *lastPrune < kPruneIntervalSeconds) {
            return;
        }
        lastPrune = now;
    }
    PruneBackups(now);
}

void SetFileAutoSaveEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(BackupMutex());
    AutoSaveEnabledStorage() = enabled;
}

bool FileAutoSaveEnabled() {
    const std::lock_guard<std::mutex> lock(BackupMutex());
    return AutoSaveEnabledStorage();
}

void SetBackupMaxAgeDays(int days) {
    const std::lock_guard<std::mutex> lock(BackupMutex());
    MaxAgeDaysStorage() = days;
}

int BackupMaxAgeDays() {
    const std::lock_guard<std::mutex> lock(BackupMutex());
    return MaxAgeDaysStorage();
}

void SetBackupMaxVersions(int versions) {
    const std::lock_guard<std::mutex> lock(BackupMutex());
    MaxVersionsStorage() = versions;
}

int BackupMaxVersions() {
    const std::lock_guard<std::mutex> lock(BackupMutex());
    return MaxVersionsStorage();
}

void ResetBackupsForTesting() {
    const std::lock_guard<std::mutex> lock(BackupMutex());
    AutoSaveEnabledStorage() = true;
    MaxAgeDaysStorage()      = 14;
    MaxVersionsStorage()     = 20;
    AutoSaveGenerationStorage().clear();
    LastPruneStorage().reset();
}

} // namespace ned::editor
