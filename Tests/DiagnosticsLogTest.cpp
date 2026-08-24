#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/DiagnosticsLog.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::LogCategory;
using ned::editor::LogCategoryFromString;
using ned::editor::LogCategoryToString;
using ned::editor::LogCategoryVisible;
using ned::editor::LogEntries;
using ned::editor::LogGeneration;
using ned::editor::LogMaxAgeDays;
using ned::editor::LogMaxEntries;
using ned::editor::LogMessage;
using ned::editor::LogSeverity;
using ned::editor::MaybePruneLogFiles;
using ned::editor::MessagesBufferName;
using ned::editor::RebuildMessagesBuffer;
using ned::editor::ResetDiagnosticsLogForTesting;
using ned::editor::SetLogCategoryVisible;
using ned::editor::SetLogMaxAgeDays;
using ned::editor::SetLogMaxEntries;

namespace {

// Mirrors BackupTest.cpp's own EnvVarGuard exactly -- saves/restores an
// environment variable's previous state around a test.
class EnvVarGuard {
  public:
    EnvVarGuard(const char* name, const char* value) : name_(name) {
        if (const char* existing = std::getenv(name)) {
            hadPrevious_ = true;
            previous_    = existing;
        }
        if (value) {
            setenv(name, value, 1);
        }
        else {
            unsetenv(name);
        }
    }

    ~EnvVarGuard() {
        if (hadPrevious_) {
            setenv(name_.c_str(), previous_.c_str(), 1);
        }
        else {
            unsetenv(name_.c_str());
        }
    }

    EnvVarGuard(const EnvVarGuard&)            = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

  private:
    std::string name_;
    bool        hadPrevious_ = false;
    std::string previous_;
};

// One disposable sandbox per test: a temp root serving as XDG_STATE_HOME (so
// the on-disk log lands inside it), plus resetting every mutex-guarded
// static on the way in and out -- Backup.h's BackupSandbox/
// BackupSettingsGuard precedent, merged into one struct since this module
// has no separate "settings vs. entries" split worth two guards.
struct LogSandbox {
    explicit LogSandbox(const std::string& name)
        : root(std::filesystem::temp_directory_path() / name), state(root / "state"), stateGuard("XDG_STATE_HOME", state.c_str()),
          homeGuard("HOME", nullptr) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(state);
        ResetDiagnosticsLogForTesting();
    }

    ~LogSandbox() {
        ResetDiagnosticsLogForTesting();
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::filesystem::path root;
    std::filesystem::path state;
    EnvVarGuard            stateGuard;
    EnvVarGuard            homeGuard;
};

std::filesystem::path LogsDir(const LogSandbox& sandbox) {
    return sandbox.state / "ned" / "logs";
}

} // namespace

TEST_CASE("LogCategoryFromString/ToString round-trips every category", "[DiagnosticsLog]") {
    for (const LogCategory category : {LogCategory::General, LogCategory::Janet, LogCategory::Lsp, LogCategory::Dap, LogCategory::Acp,
                                        LogCategory::Vcs, LogCategory::Task, LogCategory::Subprocess}) {
        const std::string name = std::string(LogCategoryToString(category));
        REQUIRE(LogCategoryFromString(name) == category);
    }
    REQUIRE_FALSE(LogCategoryFromString("not-a-real-category").has_value());
}

TEST_CASE("Lsp defaults hidden, every other category defaults visible", "[DiagnosticsLog]") {
    const LogSandbox sandbox("ned_difflog_test_defaults");
    REQUIRE_FALSE(LogCategoryVisible(LogCategory::Lsp));
    REQUIRE(LogCategoryVisible(LogCategory::General));
    REQUIRE(LogCategoryVisible(LogCategory::Janet));
    REQUIRE(LogCategoryVisible(LogCategory::Dap));
    REQUIRE(LogCategoryVisible(LogCategory::Acp));
    REQUIRE(LogCategoryVisible(LogCategory::Vcs));
    REQUIRE(LogCategoryVisible(LogCategory::Task));
    REQUIRE(LogCategoryVisible(LogCategory::Subprocess));
}

TEST_CASE("SetLogCategoryVisible round-trips and only bumps the generation on real change", "[DiagnosticsLog]") {
    const LogSandbox sandbox("ned_difflog_test_visibility");
    const std::size_t before = LogGeneration();

    SetLogCategoryVisible(LogCategory::Lsp, true);
    REQUIRE(LogCategoryVisible(LogCategory::Lsp));
    REQUIRE(LogGeneration() == before + 1);

    SetLogCategoryVisible(LogCategory::Lsp, true); // no-op: already true
    REQUIRE(LogGeneration() == before + 1);

    SetLogCategoryVisible(LogCategory::Lsp, false);
    REQUIRE_FALSE(LogCategoryVisible(LogCategory::Lsp));
    REQUIRE(LogGeneration() == before + 2);
}

TEST_CASE("LogMessage appends, bumps generation, and is visible via LogEntries", "[DiagnosticsLog]") {
    const LogSandbox sandbox("ned_difflog_test_append");
    const std::size_t before = LogGeneration();

    LogMessage(LogCategory::Janet, LogSeverity::Error, "boom", std::string("init.janet"), 12);

    REQUIRE(LogGeneration() == before + 1);
    const std::vector<ned::editor::LogEntry> entries = LogEntries();
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].category == LogCategory::Janet);
    REQUIRE(entries[0].severity == LogSeverity::Error);
    REQUIRE(entries[0].message == "boom");
    REQUIRE(entries[0].path == "init.janet");
    REQUIRE(entries[0].line == 12);
}

TEST_CASE("SetLogMaxEntries caps the ring, evicting oldest first, and trims immediately on decrease", "[DiagnosticsLog]") {
    const LogSandbox sandbox("ned_difflog_test_cap");
    REQUIRE(LogMaxEntries() == 5000);

    SetLogMaxEntries(3);
    for (int i = 0; i < 5; ++i) {
        LogMessage(LogCategory::General, LogSeverity::Info, "entry " + std::to_string(i));
    }
    std::vector<ned::editor::LogEntry> entries = LogEntries();
    REQUIRE(entries.size() == 3);
    REQUIRE(entries.front().message == "entry 2"); // 0 and 1 evicted
    REQUIRE(entries.back().message == "entry 4");

    SetLogMaxEntries(1); // decrease trims immediately, not on the next LogMessage
    entries = LogEntries();
    REQUIRE(entries.size() == 1);
    REQUIRE(entries.front().message == "entry 4");
}

TEST_CASE("RebuildMessagesBuffer renders only currently-visible categories, oldest first", "[DiagnosticsLog]") {
    const LogSandbox   sandbox("ned_difflog_test_rebuild");
    ned::text::BufferList bufferList;

    SetLogCategoryVisible(LogCategory::Lsp, false); // default, explicit for clarity
    LogMessage(LogCategory::Janet, LogSeverity::Error, "first");
    LogMessage(LogCategory::Lsp, LogSeverity::Warning, "hidden");
    LogMessage(LogCategory::Vcs, LogSeverity::Info, "second");

    RebuildMessagesBuffer(bufferList);

    ned::text::Buffer* messages = bufferList.Find(std::string(MessagesBufferName()));
    REQUIRE(messages != nullptr);
    REQUIRE(messages->ReadOnly());

    const std::string content = messages->Text();
    REQUIRE(content.find("first") != std::string::npos);
    REQUIRE(content.find("second") != std::string::npos);
    REQUIRE(content.find("hidden") == std::string::npos);
    REQUIRE(content.find("first") < content.find("second")); // oldest first

    // One synthetic Diagnostic per visible line -- Lsp's hidden entry must
    // not have contributed one.
    REQUIRE(messages->Diagnostics().size() == 2);
}

TEST_CASE("RebuildMessagesBuffer maps severity to the right Diagnostic::Severity", "[DiagnosticsLog]") {
    const LogSandbox   sandbox("ned_difflog_test_severity_map");
    ned::text::BufferList bufferList;

    LogMessage(LogCategory::General, LogSeverity::Error, "e");
    LogMessage(LogCategory::General, LogSeverity::Warning, "w");
    LogMessage(LogCategory::General, LogSeverity::Info, "i");

    RebuildMessagesBuffer(bufferList);
    ned::text::Buffer* messages = bufferList.Find(std::string(MessagesBufferName()));
    REQUIRE(messages != nullptr);

    const std::vector<ned::text::Buffer::Diagnostic>& diagnostics = messages->Diagnostics();
    REQUIRE(diagnostics.size() == 3);
    REQUIRE(diagnostics[0].severity == ned::text::Buffer::Diagnostic::Severity::Error);
    REQUIRE(diagnostics[1].severity == ned::text::Buffer::Diagnostic::Severity::Warning);
    REQUIRE(diagnostics[2].severity == ned::text::Buffer::Diagnostic::Severity::Information);
}

TEST_CASE("RebuildMessagesBuffer is idempotent (find-or-create, not duplicate-create)", "[DiagnosticsLog]") {
    const LogSandbox   sandbox("ned_difflog_test_idempotent");
    ned::text::BufferList bufferList;

    LogMessage(LogCategory::General, LogSeverity::Info, "one");
    RebuildMessagesBuffer(bufferList);
    RebuildMessagesBuffer(bufferList);

    REQUIRE(bufferList.Find(std::string(MessagesBufferName())) != nullptr);
    // A second CreateBuffer under the same name would have uniquified to
    // "*Messages*<2>" -- confirm that never happened.
    REQUIRE(bufferList.Find("*Messages*<2>") == nullptr);
}

TEST_CASE("LogMessage appends a matching line to today's on-disk log file", "[DiagnosticsLog]") {
    const LogSandbox sandbox("ned_difflog_test_disk");

    LogMessage(LogCategory::Task, LogSeverity::Error, "disk-write-check");

    bool found = false;
    for (const auto& entry : std::filesystem::directory_iterator(LogsDir(sandbox))) {
        std::ifstream in(entry.path());
        const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (content.find("disk-write-check") != std::string::npos) {
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("MaybePruneLogFiles deletes a daily log file past the age limit and keeps a newer one", "[DiagnosticsLog]") {
    const LogSandbox sandbox("ned_difflog_test_prune_age");
    std::filesystem::create_directories(LogsDir(sandbox));

    // Retention is name-based (the date is already encoded in the daily
    // filename), not mtime-based -- see DateLabelToEpochSeconds's own doc
    // comment -- so this needs no real filesystem timestamp fakery, just an
    // injected nowSeconds far enough past both dates.
    const std::filesystem::path oldFile = LogsDir(sandbox) / "ned-2020-01-01.log";
    const std::filesystem::path newFile = LogsDir(sandbox) / "ned-2020-01-13.log";
    std::ofstream(oldFile) << "old\n";
    std::ofstream(newFile) << "new\n";

    // 2020-01-15 00:00:00 UTC -- 14 days past oldFile's date, 2 days past newFile's.
    const std::int64_t now = 1579046400;
    SetLogMaxAgeDays(10);
    MaybePruneLogFiles(now);

    REQUIRE_FALSE(std::filesystem::exists(oldFile));
    REQUIRE(std::filesystem::exists(newFile));
}

TEST_CASE("MaybePruneLogFiles with a non-positive age limit disables pruning", "[DiagnosticsLog]") {
    const LogSandbox sandbox("ned_difflog_test_prune_disabled");
    std::filesystem::create_directories(LogsDir(sandbox));

    const std::filesystem::path oldFile = LogsDir(sandbox) / "ned-2020-01-01.log";
    std::ofstream(oldFile) << "old\n";

    SetLogMaxAgeDays(0);
    MaybePruneLogFiles(1755700000); // far future -- would age out if pruning were enabled

    REQUIRE(std::filesystem::exists(oldFile));
}

TEST_CASE("MaybePruneLogFiles runs at most once per hour", "[DiagnosticsLog]") {
    const LogSandbox sandbox("ned_difflog_test_prune_rate_limit");
    std::filesystem::create_directories(LogsDir(sandbox));
    SetLogMaxAgeDays(1);

    MaybePruneLogFiles(1755700000); // stamps the last-run time, well past the file's own date

    const std::filesystem::path stale = LogsDir(sandbox) / "ned-2020-01-01.log";
    std::ofstream(stale) << "stale\n";

    MaybePruneLogFiles(1755700000 + 60); // within the hour -- skipped
    REQUIRE(std::filesystem::exists(stale));

    MaybePruneLogFiles(1755700000 + 3601); // past the hour -- prunes
    REQUIRE_FALSE(std::filesystem::exists(stale));
}
