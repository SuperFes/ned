#include "KeyCompletion.h"

#include <cstdio>
#include <map>
#include <mutex>
#include <utility>

#include "Editor/DiagnosticsLog.h"
#include "IssueKey.h"
#include "Registry.h"
#include "Runner.h"

namespace ned::editor::tracker {

namespace {

    bool IsDigit(char c) {
        return c >= '0' && c <= '9';
    }
    bool IsUpper(char c) {
        return c >= 'A' && c <= 'Z';
    }
    bool IsWordChar(char c) {
        return IsDigit(c) || IsUpper(c) || (c >= 'a' && c <= 'z') || c == '_';
    }

    // LSP's CompletionItemKind.Reference.
    constexpr int kKindReference = 18;

    // The popup truncates a row's label to fit its detail, and the key is
    // the part that must stay readable: a long title gives way instead.
    std::string ShortTitle(const std::string& title) {
        constexpr std::size_t kMaxCodepoints = 40;
        std::size_t           codepoints     = 0;
        for (std::size_t at = 0; at < title.size(); ++at) {
            if ((static_cast<unsigned char>(title[at]) & 0xC0) != 0x80 && codepoints++ == kMaxCodepoints) {
                return title.substr(0, at) + "…";
            }
        }
        return title;
    }

    struct Attempts {
        std::mutex                                                   mutex;
        std::map<std::string, std::chrono::steady_clock::time_point> byCache;
    };

    Attempts& CacheAttempts() {
        static Attempts attempts;
        return attempts;
    }

    // Whether cache may be fetched now, recording the attempt if so.
    bool Due(const std::string& cache, std::chrono::steady_clock::time_point now) {
        Attempts&             attempts = CacheAttempts();
        const std::lock_guard lock(attempts.mutex);
        const auto            found = attempts.byCache.find(cache);
        if (found != attempts.byCache.end() && now - found->second < kKeyCacheLifetime) {
            return false;
        }
        attempts.byCache[cache] = now;
        return true;
    }

} // namespace

std::optional<std::size_t> IssueKeyPrefixStart(std::string_view before, const std::set<std::string>& projectKeys, bool numericKeys) {
    std::size_t digits = before.size();
    while (digits > 0 && IsDigit(before[digits - 1])) {
        --digits;
    }
    if (digits == 0) {
        return std::nullopt;
    }
    const char marker = before[digits - 1];
    if (marker == '#') {
        const std::size_t start = digits - 1;
        if (!numericKeys || (start > 0 && (IsWordChar(before[start - 1]) || before[start - 1] == '#' || before[start - 1] == '&'))) {
            return std::nullopt;
        }
        return start;
    }
    if (marker != '-') {
        return std::nullopt;
    }
    const std::size_t dash  = digits - 1;
    std::size_t       start = dash;
    while (start > 0 && (IsUpper(before[start - 1]) || IsDigit(before[start - 1]) || before[start - 1] == '_')) {
        --start;
    }
    if (start == dash || !IsUpper(before[start]) || (start > 0 && IsWordChar(before[start - 1])) ||
        !projectKeys.contains(std::string(before.substr(start, dash - start)))) {
        return std::nullopt;
    }
    return start;
}

std::vector<Completion> IssueKeyCompletions(const std::vector<Issue>& issues, std::string_view prefix) {
    const std::size_t       marker  = prefix.find_first_of("-#");
    const std::string_view  project = marker == std::string_view::npos ? prefix : prefix.substr(0, marker + 1);
    std::vector<Completion> completions;
    for (const Issue& issue : issues) {
        if (!issue.key.starts_with(project)) {
            continue;
        }
        // The session ranks equal matches by sortText: keep the tracker's
        // own order (most recently updated first) rather than the keys'.
        char order[16];
        std::snprintf(order, sizeof(order), "%08zu", completions.size());
        completions.push_back(Completion{
            .label         = issue.key,
            .filterText    = issue.key,
            .sortText      = order,
            .insertText    = issue.key,
            .source        = CompletionSource::IssueKey,
            .kind          = kKindReference,
            .detail        = ShortTitle(issue.title),
            .documentation = issue.status.empty() ? issue.title : issue.title + "\n\nStatus: " + issue.status,
        });
    }
    return completions;
}

std::optional<KeyInLine> IssueKeyAt(std::string_view line, std::size_t offset, const std::set<std::string>& projectKeys, bool numericKeys) {
    for (std::size_t start = 0; start < line.size() && start <= offset; ++start) {
        if (start > 0 && (IsWordChar(line[start - 1]) || line[start - 1] == '#' || line[start - 1] == '&')) {
            continue;
        }
        std::size_t digitsStart = 0;
        if (line[start] == '#') {
            if (!numericKeys) {
                continue;
            }
            digitsStart = start + 1;
        }
        else if (IsUpper(line[start])) {
            std::size_t dash = start;
            while (dash < line.size() && (IsUpper(line[dash]) || IsDigit(line[dash]) || line[dash] == '_')) {
                ++dash;
            }
            if (dash >= line.size() || line[dash] != '-' || !projectKeys.contains(std::string(line.substr(start, dash - start)))) {
                continue;
            }
            digitsStart = dash + 1;
        }
        else {
            continue;
        }
        std::size_t end = digitsStart;
        while (end < line.size() && IsDigit(line[end])) {
            ++end;
        }
        if (end == digitsStart || (end < line.size() && IsWordChar(line[end]))) {
            continue;
        }
        if (offset <= end) {
            return KeyInLine{.key = std::string(line.substr(start, end - start)), .start = start, .end = end};
        }
    }
    return std::nullopt;
}

std::optional<std::string> ConnectionForKey(const std::string& key) {
    if (auto listed = ConnectionForIssue(key)) {
        return listed;
    }
    if (key.starts_with('#')) {
        for (const Connection& connection : Connections()) {
            const auto provider = FindProvider(connection.provider);
            if (provider && provider->NumericKeys()) {
                return connection.name;
            }
        }
        return std::nullopt;
    }
    const std::size_t dash = key.rfind('-');
    return dash == std::string::npos ? std::nullopt : ConnectionForProjectKey(key.substr(0, dash));
}

void RefreshKeyCaches(Runner& runner, std::chrono::steady_clock::time_point now) {
    const auto logFailure = [](std::string error) { LogMessage(LogCategory::Tracker, LogSeverity::Warning, error); };
    for (const Connection& connection : Connections()) {
        const auto provider = FindProvider(connection.provider);
        if (!provider) {
            continue;
        }
        if (provider->Supports(Capability::ProjectKeys) && Due("project-keys:" + connection.name, now)) {
            runner.RequestProjectKeys(
                connection.name, [name = connection.name](std::vector<std::string> keys) { SetProjectKeys(name, std::move(keys)); },
                logFailure);
        }
        if (provider->Supports(Capability::Mine) && Due("mine:" + connection.name, now)) {
            runner.RequestMine(
                connection.name, [name = connection.name](std::vector<Issue> issues) { SetMineIssues(name, std::move(issues)); },
                logFailure);
        }
    }
}

void ResetKeyCacheAttempts() {
    Attempts&             attempts = CacheAttempts();
    const std::lock_guard lock(attempts.mutex);
    attempts.byCache.clear();
}

} // namespace ned::editor::tracker
