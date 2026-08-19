#include "GitIgnore.h"

#include <fstream>

namespace ned::editor {

namespace {

    // Escapes a single regex-metacharacter for literal matching; '*'/'?'
    // are handled separately by the caller (glob wildcards, not literals).
    void AppendEscaped(std::string& out, char ch) {
        static constexpr std::string_view kSpecial = R"(.^$+()[]{}|\)";
        if (kSpecial.find(ch) != std::string_view::npos) {
            out += '\\';
        }
        out += ch;
    }

    // Translates a single .gitignore glob pattern (already stripped of its
    // leading '!'/trailing '/') into an anchored ECMAScript regex body --
    // see GitIgnore.h's own header comment for the exact supported subset.
    std::string TranslateGlobToRegex(const std::string& pattern, bool anchored) {
        std::string body;
        for (const char ch : pattern) {
            if (ch == '*') {
                body += "[^/]*";
            }
            else if (ch == '?') {
                body += "[^/]";
            }
            else {
                AppendEscaped(body, ch);
            }
        }

        std::string full = anchored ? "^" : "^(?:.*/)?";
        full += body;
        full += '$';
        return full;
    }

} // namespace

GitIgnoreMatcher::GitIgnoreMatcher(const std::filesystem::path& root) {
    std::ifstream file(root / ".gitignore");
    if (!file) {
        return; // no .gitignore -- nothing to exclude beyond the caller's own existing checks
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
            rules_.push_back(Rule{
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

bool GitIgnoreMatcher::IsIgnored(const std::filesystem::path& relativePath, bool isDirectory) const {
    const std::string pathText = relativePath.generic_string(); // '/' separators regardless of platform

    bool ignored = false;
    for (const Rule& rule : rules_) {
        if (rule.directoryOnly && !isDirectory) {
            continue;
        }
        if (std::regex_match(pathText, rule.pattern)) {
            ignored = !rule.negated; // later rules win over earlier ones
        }
    }
    return ignored;
}

} // namespace ned::editor
