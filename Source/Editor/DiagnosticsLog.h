//
// A process-wide, best-effort diagnostics/error log -- the durable record
// ned's own errors otherwise never get. Every error path in this codebase
// currently either flashes through the one-row EchoArea and vanishes, or (a
// malformed LSP/DAP/ACP frame, a Janet stacktrace) is silently discarded
// entirely. This module is the shared sink those paths feed into: an
// in-memory ring (capped, size configurable -- infinity doesn't exist in
// RAM), a best-effort on-disk append under $XDG_STATE_HOME/ned/logs/, and a
// filterable "*Messages*" buffer rendered from the in-memory ring.
//
// Deliberately a diagnostic aid, not critical-system infrastructure: a
// dropped log line or a failed disk write is never allowed to surface as a
// user-facing error or crash. Callers on a background thread (LSP/DAP/ACP
// read loops, subprocess completion) must marshal onto the main thread via
// EventLoop::Post before calling LogMessage, same convention as every other
// cross-thread mutation in this codebase -- this module does no locking of
// its own beyond the usual mutex-guarded-static-state shape (TabWidth.h's
// pattern), which guards against nothing more than that convention being
// violated by mistake.
//

#ifndef NED_EDITOR_DIAGNOSTICSLOG_H
#define NED_EDITOR_DIAGNOSTICSLOG_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Text/BufferList.h"

namespace ned::editor {

// One entry per LogMessage call. Every subsystem gets its own category so
// SetLogCategoryVisible can hide a noisy source (Lsp defaults hidden) without
// discarding it -- toggling a category back on re-shows history already
// captured, since the in-memory ring, not the buffer's own bytes, is the
// source of truth.
enum class LogCategory { General,
                          Janet,
                          Lsp,
                          Dap,
                          Acp,
                          Vcs,
                          Task,
                          Subprocess };

enum class LogSeverity { Info,
                          Warning,
                          Error };

struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogCategory                           category;
    LogSeverity                           severity;
    std::string                           message;
    // Set together when the entry names a real source location -- lets the
    // rendered "*Messages*" buffer line end in a literal "path:line:"
    // substring, which BufferView::VisitResultUnderPoint's existing regex
    // fallback already knows how to click/Enter-visit, with no new command
    // needed. Not populated for Janet errors specifically: verified (see
    // Janet/Environment.h's DoStringCapturingStacktrace doc comment) that
    // Janet's own captured error text never carries a location, only its
    // separate, uncapturable-via-*out raw stderr print does.
    std::optional<std::string> path;
    std::optional<std::size_t> line; // 1-indexed, paired with path

    // diagnostics-log-rollup follow-up. How many consecutive LogMessage
    // calls this entry represents -- a call whose category/severity/
    // message/path/line all match the ring's current last entry increments
    // that entry's count and refreshes its timestamp instead of appending a
    // new one, so a source that logs the same thing on every tick (a
    // reconnect-and-fail loop, a repeated LSP stderr warning) collapses to
    // one updating line rather than flooding *Messages*. Always 1 for a
    // freshly appended, not-yet-repeated entry.
    std::uint32_t count = 1;

    [[nodiscard]] bool operator==(const LogEntry&) const = default;
};

// Main entry point. Main-thread-only -- see this header's own doc comment.
//
// diagnostics-log-rollup follow-up: a call that exactly repeats the ring's
// current last entry (same category/severity/message/path/line) coalesces
// into it -- see LogEntry::count's own doc comment -- rather than appending
// a distinct entry every time.
void LogMessage(LogCategory category, LogSeverity severity, std::string message, std::optional<std::string> path = std::nullopt,
                 std::optional<std::size_t> line = std::nullopt);

// Process-wide settings (mutex-guarded static state, TabWidth.h's exact
// pattern), each configured from Janet: ned/set-log-category-visible,
// ned/set-log-max-entries.
void               SetLogCategoryVisible(LogCategory category, bool visible);
[[nodiscard]] bool LogCategoryVisible(LogCategory category); // Lsp defaults false, everything else true

// The in-memory ring's cap -- oldest entries are evicted past this count.
// Default 5000. A decrease trims the ring immediately, not on the next
// LogMessage call.
void                       SetLogMaxEntries(std::size_t maxEntries);
[[nodiscard]] std::size_t LogMaxEntries();

[[nodiscard]] std::optional<LogCategory> LogCategoryFromString(std::string_view name);
[[nodiscard]] std::string_view           LogCategoryToString(LogCategory category);

// A copy of the full in-memory ring, oldest first -- unfiltered (callers
// that only want currently-visible categories should check
// LogCategoryVisible themselves, same as RebuildMessagesBuffer does).
[[nodiscard]] std::vector<LogEntry> LogEntries();

// Bumped on every LogMessage call and on every SetLogCategoryVisible/
// SetLogMaxEntries call that actually changes state -- mirrors
// Buffer::ContentGeneration()'s cheap dirty-check idiom, so a caller can
// avoid rebuilding the "*Messages*" buffer on every Paint() tick.
[[nodiscard]] std::size_t LogGeneration();

// Finds-or-creates "*Messages*" and replaces its content wholesale with one
// line per entry whose category is currently visible (oldest first),
// formatted "HH:MM:SS [SEVERITY] [category] message (path:line)". Attaches
// one synthetic Buffer::Diagnostic per line (ranged over that line's own
// bytes, severity mapped from LogSeverity) so the existing diagnostics
// gutter glyph/color pipeline renders severity for free -- see
// BufferView.cpp's DiagnosticGlyphFor. Ordinary DeleteRange/InsertAtPoint
// wrapped in one BeginUndoGroup/EndUndoGroup step, not
// Buffer::ReplaceContentForLoad (that API is tied to the async-file-loader's
// IsLoading() state machine specifically).
void RebuildMessagesBuffer(text::BufferList& bufferList);

[[nodiscard]] std::string_view MessagesBufferName(); // "*Messages*"

// Retention: deletes whole daily log files under $XDG_STATE_HOME/ned/logs/
// older than LogMaxAgeDays() (default 14, <= 0 disables). nowSeconds is
// injectable for tests only (Backup.h's own convention), defaulting to the
// current time. Swallows all errors -- an unreadable/missing log directory
// is not a failure.
void MaybePruneLogFiles(std::optional<std::int64_t> nowSeconds = std::nullopt);

void              SetLogMaxAgeDays(int days);
[[nodiscard]] int LogMaxAgeDays();

// user-facing-hang-affordance follow-up (ChildProcess-hang-protection-round-2
// -- see ROADMAP.md). True once a Warning-or-Error entry has been recorded in
// a currently-visible category (SetLogCategoryVisible) and no caller has yet
// acknowledged it -- a single, process-wide "something happened" flag, not a
// per-entry/per-pane unread count, mirroring LspManager::HasUnseenLogEntry/
// AcknowledgeLogEntry's exact shape. That older mechanism is narrower (LSP
// request-level errors only, its own separate "*lsp log*" buffer); this one
// covers everything that reaches the shared *Messages* log, including every
// hang/timeout-recovery path this module's own header comment describes
// (Clipboard/ToolchainIncludePaths' read-timeout kill, an LSP/DAP/ACP stall
// disconnect, ExpireStaleRequests' synthetic timeout). An Info-severity entry
// never sets this (not actionable enough to interrupt the user for), and
// neither does an entry in a currently-hidden category (Lsp, by default) --
// a category the user chose to hide shouldn't flash a live status message
// either. Intended consumer: BufferView::Paint(), the same
// poll-once-per-frame-when-statusMessage_-is-empty idiom the LSP precedent
// already uses.
[[nodiscard]] bool HasUnseenDiagnosticsLogEntry();
void               AcknowledgeDiagnosticsLogEntry();

// Resets every mutex-guarded static (entries, category visibility, max
// entries, max age, the prune rate-limit memo) back to its default --
// Backup.h's ResetBackupsForTesting's own precedent, since these are
// process-wide state that would otherwise leak between Catch2 test cases in
// the same process.
void ResetDiagnosticsLogForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_DIAGNOSTICSLOG_H
