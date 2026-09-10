#include "InitFile.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "Text/FilePreservation.h"

namespace ned::janet {

std::filesystem::path InitFilePath() {
    if (const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME"); xdgConfigHome && *xdgConfigHome) {
        return std::filesystem::path(xdgConfigHome) / "ned" / "init.janet";
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config" / "ned" / "init.janet";
    }

    throw std::runtime_error("ned: cannot determine config directory (neither XDG_CONFIG_HOME nor HOME is set)");
}

void LoadInitFile(Environment& env) {
    const std::filesystem::path path = InitFilePath();
    if (!std::filesystem::exists(path)) {
        return;
    }
    env.DoFile(path);
}

namespace {

    // A line that is exactly `(ned/set-theme "literal")`, ignoring
    // surrounding whitespace. Deliberately not a Janet parse: the point is to
    // recognise only the one shape this function itself writes, and leave
    // everything else untouched rather than guess at it.
    bool IsSimpleSetThemeLine(std::string_view line) {
        const std::size_t begin = line.find_first_not_of(" \t");
        if (begin == std::string_view::npos) {
            return false;
        }
        std::string_view  trimmed = line.substr(begin);
        const std::size_t end     = trimmed.find_last_not_of(" \t\r");
        if (end == std::string_view::npos) {
            return false;
        }
        trimmed = trimmed.substr(0, end + 1);

        static constexpr std::string_view kPrefix = "(ned/set-theme \"";
        if (!trimmed.starts_with(kPrefix) || !trimmed.ends_with("\")")) {
            return false;
        }
        // Exactly one quoted literal in between, with no escapes and nothing
        // after the closing paren.
        const std::string_view inner = trimmed.substr(kPrefix.size(), trimmed.size() - kPrefix.size() - 2);
        return inner.find_first_of("\"\\()") == std::string_view::npos;
    }

    std::string SetThemeLine(std::string_view themeName) {
        return "(ned/set-theme \"" + std::string(themeName) + "\")";
    }

} // namespace

std::string WithSetThemeCall(std::string_view initFileText, std::string_view themeName) {
    std::vector<std::string> lines;
    std::size_t              start           = 0;
    const bool               endsWithNewline = initFileText.empty() || initFileText.back() == '\n';
    while (start <= initFileText.size()) {
        const std::size_t end = initFileText.find('\n', start);
        if (end == std::string_view::npos) {
            if (start < initFileText.size()) {
                lines.emplace_back(initFileText.substr(start));
            }
            break;
        }
        lines.emplace_back(initFileText.substr(start, end - start));
        start = end + 1;
    }

    // The *last* matching call, since a later one is what actually takes
    // effect (SetPreferredThemeName just overwrites).
    std::size_t target = lines.size();
    for (std::size_t i = lines.size(); i > 0; --i) {
        if (IsSimpleSetThemeLine(lines[i - 1])) {
            target = i - 1;
            break;
        }
    }

    if (target < lines.size()) {
        // Keep whatever indentation the existing line had.
        const std::string& existing = lines[target];
        const std::size_t  indent   = existing.find_first_not_of(" \t");
        lines[target]               = existing.substr(0, indent == std::string::npos ? 0 : indent) + SetThemeLine(themeName);
    }
    else {
        lines.push_back(SetThemeLine(themeName));
    }

    std::string result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        if (i + 1 < lines.size() || endsWithNewline || target == lines.size() - 1) {
            result += '\n';
        }
    }
    return result;
}

void WriteSetThemeCall(std::string_view themeName) {
    const std::filesystem::path path = InitFilePath();

    std::string existing;
    if (std::filesystem::exists(path)) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("ned: cannot read " + path.string());
        }
        std::ostringstream content;
        content << in.rdbuf();
        existing = content.str();
    }

    const std::string updated = WithSetThemeCall(existing, themeName);

    std::filesystem::create_directories(path.parent_path());
    const text::PreservedFileAttributes attributes = text::CaptureFileAttributes(path);

    const std::filesystem::path temporary = path.string() + ".ned-tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("ned: cannot write " + temporary.string());
        }
        out << updated;
        if (!out.flush()) {
            throw std::runtime_error("ned: failed writing " + temporary.string());
        }
    }
    std::filesystem::rename(temporary, path);
    text::ApplyFileAttributes(path, attributes);
}

} // namespace ned::janet
