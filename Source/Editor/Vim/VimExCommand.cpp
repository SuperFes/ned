#include "VimExCommand.h"

#include <algorithm>
#include <cctype>

namespace ned::editor::vim {

namespace {

    void TrimLeadingSpaces(std::string_view& s) {
        while (!s.empty() && s.front() == ' ') {
            s.remove_prefix(1);
        }
    }

    // One ex address: '.', '$', a decimal line number (1-based, converted to 0-based
    // here), or '<'/'>' after a leading quote (the visual-selection marks). nullopt
    // means "no address token here" (not an error -- e.g. a bare command with no range).
    std::optional<std::size_t> ParseAddr(std::string_view& s, std::size_t currentLine, std::size_t lastLine,
                                         const std::optional<ExRange>& visualRange, bool wantStart) {
        if (s.empty()) {
            return std::nullopt;
        }
        if (s.front() == '.') {
            s.remove_prefix(1);
            return currentLine;
        }
        if (s.front() == '$') {
            s.remove_prefix(1);
            return lastLine;
        }
        if (s.front() == '\'' && s.size() >= 2 && (s[1] == '<' || s[1] == '>')) {
            const bool isStart = s[1] == '<';
            s.remove_prefix(2);
            if (!visualRange) {
                return currentLine;
            }
            return isStart ? visualRange->startLine : visualRange->endLine;
        }
        if (std::isdigit(static_cast<unsigned char>(s.front()))) {
            std::size_t n        = 0;
            std::size_t consumed = 0;
            while (consumed < s.size() && std::isdigit(static_cast<unsigned char>(s[consumed]))) {
                n = n * 10 + static_cast<std::size_t>(s[consumed] - '0');
                ++consumed;
            }
            s.remove_prefix(consumed);
            (void)wantStart;
            return n > 0 ? n - 1 : 0; // vim addresses are 1-based; ":0" clamps to line 0
        }
        return std::nullopt;
    }

} // namespace

std::optional<ExCommand> ParseExCommand(std::string_view text, std::size_t currentLine, std::size_t lastLine,
                                        std::optional<ExRange> visualRange) {
    std::string_view s = text;
    TrimLeadingSpaces(s);
    if (s.empty()) {
        return std::nullopt;
    }

    ExRange range{false, 0, 0};
    if (s.front() == '%') {
        s.remove_prefix(1);
        range = ExRange{true, 0, lastLine};
    }
    else {
        const std::optional<std::size_t> a1 = ParseAddr(s, currentLine, lastLine, visualRange, true);
        if (a1) {
            std::size_t start = *a1;
            std::size_t end   = *a1;
            if (!s.empty() && s.front() == ',') {
                s.remove_prefix(1);
                const std::optional<std::size_t> a2 = ParseAddr(s, currentLine, lastLine, visualRange, false);
                if (a2) {
                    end = *a2;
                }
            }
            range = ExRange{true, std::min(start, end), std::max(start, end)};
        }
    }
    TrimLeadingSpaces(s);

    ExCommand cmd;
    cmd.range = range;

    if (s.empty()) {
        return cmd; // a bare range, e.g. ":42" or ":'<,'>" -- VimEngine goes to range.endLine
    }

    // ":s" takes an arbitrary punctuation delimiter right after the letter, not a space
    // -- must be recognized before the generic alphabetic-word scan below would instead
    // consume "s" and stop at the delimiter as if it were a separating space.
    if (s.front() == 's' && (s.size() == 1 || !(std::isalnum(static_cast<unsigned char>(s[1])) || s[1] == '_'))) {
        cmd.name = "s";
        s.remove_prefix(1);
        cmd.rest = std::string(s);
        return cmd;
    }

    // ":>"/":<" (range indent/outdent) -- neither character is alphabetic, so the generic
    // command-word scan below would never recognize them. Real vim lets the character
    // repeat (":>>>" shifts three times); this codebase's own VimEngine::ExecuteExCommand
    // deliberately only ever shifts once regardless of how many were typed (documented v1
    // cut), but the repeats are still consumed here so trailing input doesn't get
    // misparsed as something else.
    if (s.front() == '>' || s.front() == '<') {
        const char shiftChar = s.front();
        while (!s.empty() && s.front() == shiftChar) {
            s.remove_prefix(1);
        }
        cmd.name = std::string(1, shiftChar);
        if (!s.empty() && s.front() == ' ') {
            s.remove_prefix(1);
        }
        cmd.rest = std::string(s);
        return cmd;
    }

    std::size_t nameLen = 0;
    while (nameLen < s.size() && std::isalpha(static_cast<unsigned char>(s[nameLen]))) {
        ++nameLen;
    }
    if (nameLen == 0) {
        return std::nullopt; // not a well-formed command word
    }
    cmd.name = std::string(s.substr(0, nameLen));
    s.remove_prefix(nameLen);
    if (!s.empty() && s.front() == '!') {
        cmd.bang = true;
        s.remove_prefix(1);
    }
    if (!s.empty() && s.front() == ' ') {
        s.remove_prefix(1); // exactly one separating space, matching vim's own :normal
    }
    cmd.rest = std::string(s);
    return cmd;
}

std::optional<ExSubstituteArgs> ParseSubstituteArgs(std::string_view rest) {
    if (rest.empty()) {
        return std::nullopt;
    }
    const char delim = rest.front();
    if (std::isalnum(static_cast<unsigned char>(delim)) || delim == ' ') {
        return std::nullopt;
    }
    rest.remove_prefix(1);

    const auto findUnescaped = [delim](std::string_view s) -> std::size_t {
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\') {
                ++i;
                continue;
            }
            if (s[i] == delim) {
                return i;
            }
        }
        return std::string_view::npos;
    };

    const std::size_t p1 = findUnescaped(rest);
    const std::string pattern(rest.substr(0, p1 == std::string_view::npos ? rest.size() : p1));
    if (p1 == std::string_view::npos) {
        return ExSubstituteArgs{pattern, "", ""};
    }
    rest.remove_prefix(p1 + 1);

    const std::size_t p2 = findUnescaped(rest);
    const std::string replacement(rest.substr(0, p2 == std::string_view::npos ? rest.size() : p2));
    std::string       flags;
    if (p2 != std::string_view::npos) {
        rest.remove_prefix(p2 + 1);
        flags = std::string(rest);
    }
    return ExSubstituteArgs{pattern, replacement, flags};
}

std::optional<std::size_t> ParseExAddress(std::string_view text, std::size_t currentLine, std::size_t lastLine,
                                          std::optional<ExRange> visualRange) {
    TrimLeadingSpaces(text);
    return ParseAddr(text, currentLine, lastLine, visualRange, true);
}

std::optional<ExGlobalArgs> ParseGlobalArgs(std::string_view rest) {
    if (rest.empty()) {
        return std::nullopt;
    }
    const char delim = rest.front();
    if (std::isalnum(static_cast<unsigned char>(delim)) || delim == ' ') {
        return std::nullopt;
    }
    rest.remove_prefix(1);

    std::size_t p = std::string_view::npos;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '\\') {
            ++i;
            continue;
        }
        if (rest[i] == delim) {
            p = i;
            break;
        }
    }

    const std::string pattern(rest.substr(0, p == std::string_view::npos ? rest.size() : p));
    if (p == std::string_view::npos) {
        return ExGlobalArgs{pattern, ""};
    }
    rest.remove_prefix(p + 1);
    return ExGlobalArgs{pattern, std::string(rest)};
}

} // namespace ned::editor::vim
