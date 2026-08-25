#include "DiagnosticsLog.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace ned::editor {

namespace {

    std::mutex& LogMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::deque<LogEntry>& EntriesStorage() {
        static std::deque<LogEntry> entries;
        return entries;
    }

    std::size_t& MaxEntriesStorage() {
        static std::size_t maxEntries = 5000;
        return maxEntries;
    }

    std::size_t& GenerationStorage() {
        static std::size_t generation = 0;
        return generation;
    }

    int& MaxAgeDaysStorage() {
        static int maxAgeDays = 14;
        return maxAgeDays;
    }

    // Lsp defaults hidden (the noisy source, per this feature's own design
    // discussion); every other category defaults visible.
    std::array<bool, 8>& CategoryVisibleStorage() {
        static std::array<bool, 8> visible = {
            true,  // General
            true,  // Janet
            false, // Lsp
            true,  // Dap
            true,  // Acp
            true,  // Vcs
            true,  // Task
            true,  // Subprocess
        };
        return visible;
    }

    std::size_t CategoryIndex(LogCategory category) {
        return static_cast<std::size_t>(category);
    }

    // user-facing-hang-affordance follow-up. See HasUnseenDiagnosticsLogEntry's
    // own doc comment.
    bool& HasUnseenStorage() {
        static bool hasUnseen = false;
        return hasUnseen;
    }

    // Trims EntriesStorage() down to MaxEntriesStorage(), oldest first --
    // callers already hold LogMutex().
    void TrimToCapLocked() {
        std::deque<LogEntry>& entries = EntriesStorage();
        const std::size_t     cap     = MaxEntriesStorage();
        while (entries.size() > cap) {
            entries.pop_front();
        }
    }

    std::string LocalTimeLabel(std::chrono::system_clock::time_point timestamp) {
        const std::time_t seconds = std::chrono::system_clock::to_time_t(timestamp);
        std::tm            local{};
        localtime_r(&seconds, &local);
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
        return buffer;
    }

    // YYYY-MM-DD in UTC, the daily log file's own date key -- matches
    // Backup.cpp's VersionFileName's own gmtime_r-based convention.
    std::string UtcDateLabel(std::chrono::system_clock::time_point timestamp) {
        const std::time_t seconds = std::chrono::system_clock::to_time_t(timestamp);
        std::tm            utc{};
        gmtime_r(&seconds, &utc);
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
        return buffer;
    }

    // Inverse of UtcDateLabel, applied to a daily log file's own stem
    // ("ned-YYYY-MM-DD") -- retention is name-based, not mtime-based, since
    // the date is already encoded in the filename and this keeps
    // MaybePruneLogFiles fully deterministic under an injected nowSeconds
    // (BackupTest.cpp's PruneBackups tests get the same determinism for
    // free from encoding a version's timestamp in its own filename).
    // Returns nullopt for anything that doesn't match -- an unrelated file
    // in the logs directory is left alone rather than misparsed.
    std::optional<std::int64_t> DateLabelToEpochSeconds(const std::string& stem) {
        int year = 0, month = 0, day = 0;
        if (std::sscanf(stem.c_str(), "ned-%d-%d-%d", &year, &month, &day) != 3) {
            return std::nullopt;
        }
        std::tm utc{};
        utc.tm_year = year - 1900;
        utc.tm_mon  = month - 1;
        utc.tm_mday = day;
        return static_cast<std::int64_t>(timegm(&utc));
    }

    const char* SeverityLabel(LogSeverity severity) {
        switch (severity) {
            case LogSeverity::Info:
                return "INFO";
            case LogSeverity::Warning:
                return "WARN";
            case LogSeverity::Error:
                return "ERROR";
        }
        return "INFO"; // unreachable -- silences a "not all enumerators handled" warning on some compilers
    }

    text::Buffer::Diagnostic::Severity DiagnosticSeverityFor(LogSeverity severity) {
        switch (severity) {
            case LogSeverity::Info:
                return text::Buffer::Diagnostic::Severity::Information;
            case LogSeverity::Warning:
                return text::Buffer::Diagnostic::Severity::Warning;
            case LogSeverity::Error:
                return text::Buffer::Diagnostic::Severity::Error;
        }
        return text::Buffer::Diagnostic::Severity::Information; // unreachable
    }

    // diagnostics-log-rollup follow-up: the "(xN)" suffix rendered for a
    // coalesced entry -- shared between FormatLine and RebuildMessagesBuffer's
    // synthetic Diagnostic message so both agree on the same rendering.
    std::string CountSuffix(std::uint32_t count) {
        return count > 1 ? " (x" + std::to_string(count) + ")" : std::string();
    }

    // One formatted line for a single entry, e.g.
    // "09:15:03 [ERROR] [lsp] message text (x47) (init.janet:12)" -- the
    // "path:line:" suffix (colon-terminated) is what lets
    // BufferView::VisitResultUnderPoint's existing "^(.*):(\\d+):" regex
    // fallback click/Enter-visit it, with no new command needed.
    std::string FormatLine(const LogEntry& entry) {
        std::string line = LocalTimeLabel(entry.timestamp) + " [" + SeverityLabel(entry.severity) + "] [" +
                            std::string(LogCategoryToString(entry.category)) + "] " + entry.message + CountSuffix(entry.count);
        if (entry.path && entry.line) {
            line += " (" + *entry.path + ":" + std::to_string(*entry.line) + ":)";
        }
        return line;
    }

    // $XDG_STATE_HOME/ned/logs, falling back to $HOME/.local/state/ned/logs
    // -- the same inlined resolution Backup.cpp/Session.cpp/ProjectTrust.cpp
    // each already duplicate (no shared helper exists in this codebase to
    // extract into; matching the existing convention rather than
    // introducing one is deliberate, out of scope for this feature).
    std::filesystem::path LogsDirectory() {
        if (const char* xdgStateHome = std::getenv("XDG_STATE_HOME"); xdgStateHome && *xdgStateHome) {
            return std::filesystem::path(xdgStateHome) / "ned" / "logs";
        }
        if (const char* home = std::getenv("HOME"); home && *home) {
            return std::filesystem::path(home) / ".local" / "state" / "ned" / "logs";
        }
        throw std::runtime_error("ned: cannot determine state directory (neither XDG_STATE_HOME nor HOME is set)");
    }

    // Best-effort: a write failure here must never surface to the user or
    // throw past this function -- this is a diagnostic aid, not a source of
    // truth the editor depends on.
    void AppendToDiskBestEffort(const LogEntry& entry) {
        try {
            const std::filesystem::path directory = LogsDirectory();
            std::filesystem::create_directories(directory);
            const std::filesystem::path file = directory / ("ned-" + UtcDateLabel(entry.timestamp) + ".log");
            std::ofstream                out(file, std::ios::app);
            out << FormatLine(entry) << '\n';
        }
        catch (const std::exception&) {
            // Swallowed -- see this function's own doc comment.
        }
    }

    std::optional<std::int64_t>& LastPruneStorage() {
        static std::optional<std::int64_t> lastPrune;
        return lastPrune;
    }

    std::int64_t NowOr(std::optional<std::int64_t> nowSeconds) {
        return nowSeconds.value_or(static_cast<std::int64_t>(std::time(nullptr)));
    }

} // namespace

void LogMessage(LogCategory category, LogSeverity severity, std::string message, std::optional<std::string> path,
                 std::optional<std::size_t> line) {
    LogEntry entry{
        .timestamp = std::chrono::system_clock::now(),
        .category  = category,
        .severity  = severity,
        .message   = std::move(message),
        .path      = std::move(path),
        .line      = line,
    };

    // diagnostics-log-rollup follow-up: coalesced into diskEntry below, which
    // either is `entry` itself (first/distinct occurrence, always written)
    // or a copy of the ring's updated last entry (a repeat, written only on
    // an exponential count milestone -- 1, 2, 4, 8, ... -- so a long
    // identical-message streak still leaves a bounded, periodically updated
    // trail on disk instead of either silence or unbounded growth).
    LogEntry diskEntry         = entry;
    bool     shouldWriteToDisk = true;

    {
        const std::lock_guard<std::mutex> lock(LogMutex());
        std::deque<LogEntry>&             entries = EntriesStorage();
        LogEntry* const                   last     = entries.empty() ? nullptr : &entries.back();
        const bool repeatsLast = last && last->category == entry.category && last->severity == entry.severity &&
                                 last->message == entry.message && last->path == entry.path && last->line == entry.line;
        if (repeatsLast) {
            ++last->count;
            last->timestamp   = entry.timestamp;
            diskEntry         = *last;
            shouldWriteToDisk = (last->count & (last->count - 1)) == 0; // power-of-two milestone
        }
        else {
            entries.push_back(entry);
            TrimToCapLocked();
        }
        ++GenerationStorage();

        // user-facing-hang-affordance follow-up: Info-severity entries never
        // set this (not actionable enough to interrupt the user for), and
        // neither does a category the user has hidden -- reads
        // CategoryVisibleStorage() directly rather than calling the public
        // LogCategoryVisible(), which would re-lock LogMutex() (not
        // reentrant) and deadlock.
        if (severity != LogSeverity::Info && CategoryVisibleStorage()[CategoryIndex(category)]) {
            HasUnseenStorage() = true;
        }
    }

    if (shouldWriteToDisk) {
        AppendToDiskBestEffort(diskEntry);
    }
}

void SetLogCategoryVisible(LogCategory category, bool visible) {
    const std::lock_guard<std::mutex> lock(LogMutex());
    bool&                             storage = CategoryVisibleStorage()[CategoryIndex(category)];
    if (storage != visible) {
        storage = visible;
        ++GenerationStorage();
    }
}

bool LogCategoryVisible(LogCategory category) {
    const std::lock_guard<std::mutex> lock(LogMutex());
    return CategoryVisibleStorage()[CategoryIndex(category)];
}

void SetLogMaxEntries(std::size_t maxEntries) {
    const std::lock_guard<std::mutex> lock(LogMutex());
    MaxEntriesStorage() = std::max<std::size_t>(1, maxEntries);
    TrimToCapLocked(); // a decrease trims immediately, not on the next LogMessage call
    ++GenerationStorage();
}

std::size_t LogMaxEntries() {
    const std::lock_guard<std::mutex> lock(LogMutex());
    return MaxEntriesStorage();
}

std::optional<LogCategory> LogCategoryFromString(std::string_view name) {
    if (name == "general")
        return LogCategory::General;
    if (name == "janet")
        return LogCategory::Janet;
    if (name == "lsp")
        return LogCategory::Lsp;
    if (name == "dap")
        return LogCategory::Dap;
    if (name == "acp")
        return LogCategory::Acp;
    if (name == "vcs")
        return LogCategory::Vcs;
    if (name == "task")
        return LogCategory::Task;
    if (name == "subprocess")
        return LogCategory::Subprocess;
    return std::nullopt;
}

std::string_view LogCategoryToString(LogCategory category) {
    switch (category) {
        case LogCategory::General:
            return "general";
        case LogCategory::Janet:
            return "janet";
        case LogCategory::Lsp:
            return "lsp";
        case LogCategory::Dap:
            return "dap";
        case LogCategory::Acp:
            return "acp";
        case LogCategory::Vcs:
            return "vcs";
        case LogCategory::Task:
            return "task";
        case LogCategory::Subprocess:
            return "subprocess";
    }
    return "general"; // unreachable -- silences a "not all enumerators handled" warning on some compilers
}

std::vector<LogEntry> LogEntries() {
    const std::lock_guard<std::mutex> lock(LogMutex());
    const std::deque<LogEntry>&       entries = EntriesStorage();
    return std::vector<LogEntry>(entries.begin(), entries.end());
}

std::size_t LogGeneration() {
    const std::lock_guard<std::mutex> lock(LogMutex());
    return GenerationStorage();
}

std::string_view MessagesBufferName() {
    return "*Messages*";
}

void RebuildMessagesBuffer(text::BufferList& bufferList) {
    const std::string bufferName(MessagesBufferName());
    text::Buffer*      buffer = bufferList.Find(bufferName);
    if (!buffer) {
        buffer = &bufferList.CreateBuffer(bufferName);
        buffer->SetReadOnly(true); // must be set before the first append -- AppendWhileReadOnly's own precondition
    }

    std::vector<LogEntry> visible;
    for (LogEntry& entry : LogEntries()) {
        if (LogCategoryVisible(entry.category)) {
            visible.push_back(std::move(entry));
        }
    }

    buffer->SetReadOnly(false);
    buffer->BeginUndoGroup();
    if (buffer->Size() > 0) {
        buffer->DeleteRange(0, buffer->Size());
    }

    std::vector<text::Buffer::Diagnostic> diagnostics;
    diagnostics.reserve(visible.size());
    std::size_t offset = 0;
    for (const LogEntry& entry : visible) {
        const std::string line      = FormatLine(entry) + "\n";
        const std::size_t startByte = offset;
        buffer->InsertAtPoint(line);
        offset += line.size();
        // -1 excludes the trailing newline from the diagnostic's own range,
        // matching every other line-ranged Diagnostic in this codebase.
        diagnostics.push_back(text::Buffer::Diagnostic{
            .startByte = startByte,
            .endByte   = offset > 0 ? offset - 1 : offset,
            .severity  = DiagnosticSeverityFor(entry.severity),
            .origin    = text::Buffer::Diagnostic::Origin::Code,
            .message   = entry.message + CountSuffix(entry.count),
        });
    }
    buffer->SetPoint(buffer->Size());
    buffer->EndUndoGroup();
    buffer->SetReadOnly(true);
    buffer->SetDiagnostics(std::move(diagnostics));
}

void SetLogMaxAgeDays(int days) {
    const std::lock_guard<std::mutex> lock(LogMutex());
    MaxAgeDaysStorage() = days;
}

int LogMaxAgeDays() {
    const std::lock_guard<std::mutex> lock(LogMutex());
    return MaxAgeDaysStorage();
}

void MaybePruneLogFiles(std::optional<std::int64_t> nowSeconds) {
    const int maxAgeDays = LogMaxAgeDays();
    if (maxAgeDays <= 0) {
        return;
    }

    const std::int64_t now = NowOr(nowSeconds);
    {
        const std::lock_guard<std::mutex> lock(LogMutex());
        constexpr std::int64_t            kPruneIntervalSeconds = 3600; // once per hour of process lifetime
        std::optional<std::int64_t>&      lastPrune             = LastPruneStorage();
        if (lastPrune && now - *lastPrune < kPruneIntervalSeconds) {
            return;
        }
        lastPrune = now;
    }

    const std::int64_t cutoff = now - static_cast<std::int64_t>(maxAgeDays) * 86400;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(LogsDirectory())) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::optional<std::int64_t> fileDate = DateLabelToEpochSeconds(entry.path().stem().string());
            if (fileDate && *fileDate < cutoff) {
                std::error_code ec;
                std::filesystem::remove(entry.path(), ec);
            }
        }
    }
    catch (const std::exception&) {
        // No logs directory yet, or it can't be listed -- nothing to prune.
    }
}

bool HasUnseenDiagnosticsLogEntry() {
    const std::lock_guard<std::mutex> lock(LogMutex());
    return HasUnseenStorage();
}

void AcknowledgeDiagnosticsLogEntry() {
    const std::lock_guard<std::mutex> lock(LogMutex());
    HasUnseenStorage() = false;
}

void ResetDiagnosticsLogForTesting() {
    const std::lock_guard<std::mutex> lock(LogMutex());
    EntriesStorage().clear();
    MaxEntriesStorage()    = 5000;
    MaxAgeDaysStorage()    = 14;
    GenerationStorage()    = 0;
    CategoryVisibleStorage() = {
        true, true, false, true, true, true, true, true,
    };
    LastPruneStorage().reset();
    HasUnseenStorage() = false;
}

} // namespace ned::editor
