#include "GitIgnore.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string_view>

namespace ned::editor {

namespace {

    // Escapes a single regex-metacharacter for literal matching; '*'/'?'/
    // '[' are handled separately by the caller (glob wildcards, not
    // literals).
    void AppendEscaped(std::string& out, char ch) {
        static constexpr std::string_view kSpecial = R"(.^$+()[]{}|\)";
        if (kSpecial.find(ch) != std::string_view::npos) {
            out += '\\';
        }
        out += ch;
    }

    // The index of the closing ']' of a character class opening at
    // pattern[open] (== '['), or npos if unterminated. Mirrors fnmatch's
    // own convention: a ']' immediately after '[' (or '[!'/'[^') is a
    // literal member, not the terminator.
    std::size_t FindClassEnd(const std::string& pattern, std::size_t open) {
        std::size_t j = open + 1;
        if (j < pattern.size() && (pattern[j] == '!' || pattern[j] == '^')) {
            ++j;
        }
        if (j < pattern.size() && pattern[j] == ']') {
            ++j;
        }
        while (j < pattern.size() && pattern[j] != ']') {
            ++j;
        }
        return j < pattern.size() ? j : std::string::npos;
    }

    void AppendClass(std::string& out, const std::string& pattern, std::size_t open, std::size_t close) {
        out += '[';
        std::size_t j = open + 1;
        if (pattern[j] == '!' || pattern[j] == '^') { // git's wildmatch accepts either negation marker
            out += '^';
            ++j;
        }
        for (; j < close; ++j) {
            const char ch = pattern[j];
            if (ch == '\\' || ch == ']' || ch == '[') {
                out += '\\';
            }
            out += ch;
        }
        out += ']';
    }

    // Translates a single .gitignore glob pattern (already stripped of its
    // leading '!'/trailing '/') into an anchored ECMAScript regex body --
    // see GitIgnore.h's own header comment for the exact supported subset.
    std::string TranslateGlobToRegex(const std::string& pattern, bool anchored) {
        std::string body;
        std::size_t i = 0;
        while (i < pattern.size()) {
            const char ch = pattern[i];
            // "**" only has its special meaning as a whole path segment
            // (git's own rule) -- "a**b" degrades to two ordinary '*'s.
            if (ch == '*' && i + 1 < pattern.size() && pattern[i + 1] == '*' && (i == 0 || pattern[i - 1] == '/')) {
                if (i + 2 == pattern.size()) { // trailing "/**": everything inside
                    body += ".+";
                    i += 2;
                    continue;
                }
                if (pattern[i + 2] == '/') { // "**/": zero or more whole directories
                    body += "(?:[^/]+/)*";
                    i += 3;
                    continue;
                }
            }
            if (ch == '*') {
                body += "[^/]*";
                ++i;
                continue;
            }
            if (ch == '?') {
                body += "[^/]";
                ++i;
                continue;
            }
            if (ch == '[') {
                const std::size_t close = FindClassEnd(pattern, i);
                if (close != std::string::npos) {
                    AppendClass(body, pattern, i, close);
                    i = close + 1;
                    continue;
                }
                // unterminated -- a literal '[', matching fnmatch
            }
            AppendEscaped(body, ch);
            ++i;
        }

        std::string full = anchored ? "^" : "^(?:.*/)?";
        full += body;
        full += '$';
        return full;
    }

    std::string_view Trim(std::string_view text) {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
            text.remove_prefix(1);
        }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
            text.remove_suffix(1);
        }
        return text;
    }

    bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }

    std::optional<std::string> EnvVar(const char* name) {
        const char* value = std::getenv(name);
        if (value == nullptr || *value == '\0') {
            return std::nullopt;
        }
        return std::string(value);
    }

    std::optional<std::filesystem::path> HomeDir() {
        if (const std::optional<std::string> home = EnvVar("HOME")) {
            return std::filesystem::path(*home);
        }
        return std::nullopt;
    }

    std::optional<std::filesystem::path> XdgConfigHome() {
        if (const std::optional<std::string> xdg = EnvVar("XDG_CONFIG_HOME")) {
            return std::filesystem::path(*xdg);
        }
        if (const std::optional<std::filesystem::path> home = HomeDir()) {
            return *home / ".config";
        }
        return std::nullopt;
    }

    // Extracts core.excludesFile from one git config file -- a minimal INI
    // scan of just that key (section header + "key = value" lines, quoted
    // or comment-suffixed values, leading-"~/" expansion), deliberately not
    // a full git-config parser (includes, multi-line values, and
    // subsections don't matter for this one key in practice).
    std::optional<std::filesystem::path> ParseExcludesFile(const std::filesystem::path& configPath) {
        std::ifstream file(configPath);
        if (!file) {
            return std::nullopt;
        }

        std::optional<std::filesystem::path> result;
        bool                                 inCore = false;
        std::string                          line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const std::string_view trimmed = Trim(line);
            if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') {
                continue;
            }
            if (trimmed.front() == '[') {
                inCore = EqualsIgnoreCase(trimmed, "[core]");
                continue;
            }
            if (!inCore) {
                continue;
            }

            const std::size_t eq = trimmed.find('=');
            if (eq == std::string_view::npos || !EqualsIgnoreCase(Trim(trimmed.substr(0, eq)), "excludesfile")) {
                continue;
            }

            std::string_view value = Trim(trimmed.substr(eq + 1));
            if (!value.empty() && value.front() == '"') {
                const std::size_t closing = value.find('"', 1);
                value                     = value.substr(1, closing == std::string_view::npos ? std::string_view::npos : closing - 1);
            }
            else {
                const std::size_t comment = value.find_first_of("#;");
                if (comment != std::string_view::npos) {
                    value = Trim(value.substr(0, comment));
                }
            }
            if (value.empty()) {
                continue;
            }

            if (value.size() >= 2 && value.substr(0, 2) == "~/") {
                if (const std::optional<std::filesystem::path> home = HomeDir()) {
                    result = *home / std::string(value.substr(2));
                }
                continue;
            }
            result = std::filesystem::path(std::string(value)); // later occurrences win, like git itself
        }
        return result;
    }

    std::optional<std::filesystem::file_time_type> FileMtime(const std::filesystem::path& file) {
        std::error_code                       ec;
        const std::filesystem::file_time_type mtime = std::filesystem::last_write_time(file, ec);
        if (ec) {
            return std::nullopt;
        }
        return mtime;
    }

} // namespace

GitIgnoreMatcher::GitIgnoreMatcher(const std::filesystem::path& root) : root_(root) {
    std::error_code ec;
    wasGitRepo_ = std::filesystem::exists(root_ / ".git", ec);
    if (!wasGitRepo_) {
        return; // outside a repository, global/info-exclude files mean nothing (git's own rule)
    }
    ResolveGlobalIgnore();
    ResolveInfoExclude();
}

void GitIgnoreMatcher::ResolveGlobalIgnore() {
    std::vector<std::filesystem::path> configFiles;
    if (const std::optional<std::string> override = EnvVar("GIT_CONFIG_GLOBAL")) {
        configFiles.emplace_back(*override); // replaces both global config files, per git itself
    }
    else {
        if (const std::optional<std::filesystem::path> xdg = XdgConfigHome()) {
            configFiles.push_back(*xdg / "git" / "config");
        }
        if (const std::optional<std::filesystem::path> home = HomeDir()) {
            configFiles.push_back(*home / ".gitconfig"); // read after the XDG file, so it wins -- git's own order
        }
    }

    std::optional<std::filesystem::path> ignoreFile;
    for (const std::filesystem::path& config : configFiles) {
        RecordSource(config); // an edit adding/changing core.excludesFile must invalidate the cache
        if (const std::optional<std::filesystem::path> fromConfig = ParseExcludesFile(config)) {
            ignoreFile = fromConfig;
        }
    }
    if (!ignoreFile) {
        const std::optional<std::filesystem::path> xdg = XdgConfigHome();
        if (!xdg) {
            return;
        }
        ignoreFile = *xdg / "git" / "ignore"; // git's default when core.excludesFile is unset
    }
    LoadRulesFile(*ignoreFile, globalRules_);
}

void GitIgnoreMatcher::ResolveInfoExclude() {
    const std::filesystem::path gitEntry = root_ / ".git";
    std::filesystem::path       gitDir   = gitEntry;

    std::error_code ec;
    if (std::filesystem::is_regular_file(gitEntry, ec)) {
        // A worktree/submodule checkout: .git is a one-line
        // "gitdir: <path>" pointer file, not the directory itself.
        RecordSource(gitEntry); // a repointed gitdir must invalidate the cache
        std::ifstream file(gitEntry);
        std::string   line;
        if (file && std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            constexpr std::string_view kPrefix = "gitdir:";
            const std::string_view     trimmed = Trim(line);
            if (trimmed.substr(0, kPrefix.size()) == kPrefix) {
                const std::filesystem::path target(std::string(Trim(trimmed.substr(kPrefix.size()))));
                gitDir = target.is_absolute() ? target : root_ / target;
            }
        }
    }

    LoadRulesFile(gitDir / "info" / "exclude", infoExcludeRules_);
}

void GitIgnoreMatcher::RecordSource(const std::filesystem::path& file) const {
    sources_.push_back(Source{file, FileMtime(file)});
}

void GitIgnoreMatcher::LoadRulesFile(const std::filesystem::path& path, RuleSet& out) const {
    RecordSource(path); // absence is recorded too -- a file appearing later must invalidate the cache

    std::ifstream file(path);
    if (!file) {
        return; // missing file -- nothing to exclude beyond the caller's own existing checks
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back(); // tolerate CRLF line endings
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }

        bool        negated = false;
        std::size_t start   = 0;
        if (line.front() == '!') {
            negated = true;
            start   = 1;
        }

        std::string pattern = line.substr(start);
        if (pattern.empty()) {
            continue;
        }

        bool directoryOnly = false;
        if (pattern.back() == '/') {
            directoryOnly = true;
            pattern.pop_back();
        }
        if (pattern.empty()) {
            continue; // a degenerate line like "!" or "/" alone
        }

        // Real git's own anchoring rule: a pattern with a '/' anywhere
        // other than a trailing position (already stripped above) is
        // anchored to the .gitignore's own directory; a pattern with no
        // '/' at all matches at any depth.
        bool anchored = false;
        if (pattern.front() == '/') {
            anchored = true;
            pattern.erase(0, 1);
        }
        else if (pattern.find('/') != std::string::npos) {
            anchored = true;
        }
        if (pattern.empty()) {
            continue; // a degenerate line like "/" alone
        }

        try {
            out.rules.push_back(Rule{
                .pattern       = std::regex(TranslateGlobToRegex(pattern, anchored)),
                .negated       = negated,
                .directoryOnly = directoryOnly,
            });
        }
        catch (const std::regex_error&) {
            // A line that somehow still fails to compile is skipped, not a
            // hard error -- matches this codebase's general "an unparseable
            // line in a user-authored config-like file is ignored, not
            // fatal" convention (see ThemeFile.cpp/InitFile.cpp).
        }
    }
}

std::optional<bool> GitIgnoreMatcher::RuleSet::Match(const std::string& pathText, bool isDirectory) const {
    std::optional<bool> verdict;
    for (const Rule& rule : rules) {
        if (rule.directoryOnly && !isDirectory) {
            continue;
        }
        if (std::regex_match(pathText, rule.pattern)) {
            verdict = !rule.negated; // later rules win over earlier ones
        }
    }
    return verdict;
}

const GitIgnoreMatcher::RuleSet& GitIgnoreMatcher::DirectoryRulesLocked(const std::string& relativeDir) const {
    const auto it = directoryRules_.find(relativeDir);
    if (it != directoryRules_.end()) {
        return it->second;
    }
    RuleSet&                    rules = directoryRules_[relativeDir]; // node-based map -- reference stays stable
    const std::filesystem::path file =
        relativeDir.empty() ? root_ / ".gitignore" : root_ / std::filesystem::path(relativeDir) / ".gitignore";
    LoadRulesFile(file, rules);
    return rules;
}

bool GitIgnoreMatcher::IsIgnored(const std::filesystem::path& relativePath, bool isDirectory) const {
    const std::string pathText = relativePath.generic_string(); // '/' separators regardless of platform

    std::optional<bool> verdict;
    const auto          consider = [&](const RuleSet& rules, const std::string& text) {
        if (const std::optional<bool> match = rules.Match(text, isDirectory)) {
            verdict = *match; // a later (higher-precedence) source's match wins outright
        }
    };

    consider(globalRules_, pathText);
    consider(infoExcludeRules_, pathText);

    const std::lock_guard<std::mutex> lock(mutex_);
    // Root .gitignore first, then each nested one down the parent chain --
    // each matched against the path relative to its own directory, deeper
    // files overriding shallower ones (git's own precedence).
    consider(DirectoryRulesLocked(std::string()), pathText);
    for (std::size_t pos = pathText.find('/'); pos != std::string::npos; pos = pathText.find('/', pos + 1)) {
        consider(DirectoryRulesLocked(pathText.substr(0, pos)), pathText.substr(pos + 1));
    }

    return verdict.value_or(false);
}

bool GitIgnoreMatcher::AnySourceChanged() const {
    std::error_code ec;
    if (std::filesystem::exists(root_ / ".git", ec) != wasGitRepo_) {
        return true; // a repository appearing/vanishing changes which sources even apply
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const Source& source : sources_) {
        if (FileMtime(source.file) != source.mtime) {
            return true;
        }
    }
    return false;
}

namespace {

    std::mutex& CacheMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::unordered_map<std::string, std::unique_ptr<GitIgnoreMatcher>>& Cache() {
        static std::unordered_map<std::string, std::unique_ptr<GitIgnoreMatcher>> cache;
        return cache;
    }

} // namespace

const GitIgnoreMatcher& CachedGitIgnoreMatcher(const std::filesystem::path& root) {
    const std::string key = root.generic_string();

    const std::lock_guard<std::mutex> lock(CacheMutex());
    auto&                             cache = Cache();

    auto it = cache.find(key);
    if (it == cache.end() || it->second->AnySourceChanged()) {
        it = cache.insert_or_assign(key, std::make_unique<GitIgnoreMatcher>(root)).first;
    }
    return *it->second;
}

} // namespace ned::editor
