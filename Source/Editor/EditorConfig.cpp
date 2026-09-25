#include "EditorConfig.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <deque>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <system_error>
#include <unordered_map>

#include "FormatRules.h"

namespace ned::editor {

namespace {

    std::mutex& EnabledMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& EnabledStorage() {
        static bool enabled = true;
        return enabled;
    }

    // Deeply nested braces are a malformed file, not a pattern to honour.
    constexpr int kMaxBraceDepth = 8;

    std::string Lower(std::string_view text) {
        std::string out(text);
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    std::string_view Trim(std::string_view text) {
        const std::size_t start = text.find_first_not_of(" \t\r");
        if (start == std::string_view::npos) {
            return {};
        }
        return text.substr(start, text.find_last_not_of(" \t\r") - start + 1);
    }

    std::optional<long long> ParseInteger(std::string_view text) {
        long long value = 0;
        if (!text.empty() && text.front() == '+') {
            text.remove_prefix(1);
        }
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size()) {
            return std::nullopt;
        }
        return value;
    }

    // The index of the '}' closing the '{' at `open`, or npos.
    std::size_t MatchingBrace(std::string_view pattern, std::size_t open) {
        int depth = 0;
        for (std::size_t i = open; i < pattern.size(); ++i) {
            if (pattern[i] == '\\') {
                ++i;
            }
            else if (pattern[i] == '{') {
                ++depth;
            }
            else if (pattern[i] == '}' && --depth == 0) {
                return i;
            }
        }
        return std::string_view::npos;
    }

    std::vector<std::string_view> SplitAlternatives(std::string_view body) {
        std::vector<std::string_view> parts;
        int                           depth = 0;
        std::size_t                   start = 0;
        for (std::size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '\\') {
                ++i;
            }
            else if (body[i] == '{') {
                ++depth;
            }
            else if (body[i] == '}') {
                --depth;
            }
            else if (body[i] == ',' && depth == 0) {
                parts.push_back(body.substr(start, i - start));
                start = i + 1;
            }
        }
        parts.push_back(body.substr(start));
        return parts;
    }

    // `[...]` at `open`: whether it matches `c`, and where the class ends.
    // nullopt end means no closing ']', so the '[' is a literal.
    std::optional<std::size_t> MatchClass(std::string_view pattern, std::size_t open, char c, bool& matched) {
        std::size_t i      = open + 1;
        const bool  negate = i < pattern.size() && (pattern[i] == '!' || pattern[i] == '^');
        if (negate) {
            ++i;
        }
        bool       any   = false;
        const auto first = i;
        for (; i < pattern.size(); ++i) {
            if (pattern[i] == ']' && i != first) {
                matched = (any != negate) && c != '/';
                return i;
            }
            char low = pattern[i];
            if (low == '\\' && i + 1 < pattern.size()) {
                low = pattern[++i];
            }
            char high = low;
            if (i + 2 < pattern.size() && pattern[i + 1] == '-' && pattern[i + 2] != ']') {
                high = pattern[i + 2];
                i += 2;
            }
            any = any || (c >= low && c <= high);
        }
        return std::nullopt;
    }

    bool Match(std::string_view pattern, std::string_view path, int braceDepth);

    // {n1..n2}: an integer in range, then the rest of the pattern.
    bool MatchRange(long long low, long long high, std::string_view rest, std::string_view path, int braceDepth) {
        if (low > high) {
            std::swap(low, high);
        }
        std::size_t digitsStart = (!path.empty() && (path.front() == '-' || path.front() == '+')) ? 1 : 0;
        std::size_t end         = digitsStart;
        while (end < path.size() && std::isdigit(static_cast<unsigned char>(path[end]))) {
            ++end;
        }
        for (std::size_t length = end; length > digitsStart; --length) {
            const std::optional<long long> value = ParseInteger(path.substr(0, length));
            if (value && *value >= low && *value <= high && Match(rest, path.substr(length), braceDepth)) {
                return true;
            }
        }
        return false;
    }

    bool Match(std::string_view pattern, std::string_view path, int braceDepth) {
        while (!pattern.empty()) {
            const char p = pattern.front();
            if (p == '*') {
                const bool             crossesSegments = pattern.size() > 1 && pattern[1] == '*';
                const std::string_view rest            = pattern.substr(crossesSegments ? 2 : 1);
                for (std::size_t skip = 0; skip <= path.size(); ++skip) {
                    if (Match(rest, path.substr(skip), braceDepth)) {
                        return true;
                    }
                    if (skip < path.size() && path[skip] == '/' && !crossesSegments) {
                        return false;
                    }
                }
                return false;
            }
            if (p == '{') {
                const std::size_t close = MatchingBrace(pattern, 0);
                if (close != std::string_view::npos && braceDepth < kMaxBraceDepth) {
                    const std::string_view body = pattern.substr(1, close - 1);
                    const std::string_view rest = pattern.substr(close + 1);
                    if (const std::size_t dots = body.find(".."); dots != std::string_view::npos) {
                        const auto low  = ParseInteger(body.substr(0, dots));
                        const auto high = ParseInteger(body.substr(dots + 2));
                        if (low && high) {
                            return MatchRange(*low, *high, rest, path, braceDepth + 1);
                        }
                    }
                    const std::vector<std::string_view> alternatives = SplitAlternatives(body);
                    if (alternatives.size() > 1) {
                        return std::ranges::any_of(alternatives, [&](std::string_view alternative) {
                            return Match(std::string(alternative) + std::string(rest), path, braceDepth + 1);
                        });
                    }
                }
                // A lone `{word}` or an unclosed brace is literal text.
            }
            if (path.empty()) {
                return false;
            }
            if (p == '?') {
                if (path.front() == '/') {
                    return false;
                }
                pattern.remove_prefix(1);
                path.remove_prefix(1);
                continue;
            }
            if (p == '[') {
                bool matched = false;
                if (const std::optional<std::size_t> end = MatchClass(pattern, 0, path.front(), matched)) {
                    if (!matched) {
                        return false;
                    }
                    pattern.remove_prefix(*end + 1);
                    path.remove_prefix(1);
                    continue;
                }
            }
            char literal = p;
            if (p == '\\' && pattern.size() > 1) {
                pattern.remove_prefix(1);
                literal = pattern.front();
            }
            if (literal != path.front()) {
                return false;
            }
            pattern.remove_prefix(1);
            path.remove_prefix(1);
        }
        return path.empty();
    }

    bool HasSlashOutsideBrackets(std::string_view pattern) {
        bool inClass = false;
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            if (pattern[i] == '\\') {
                ++i;
            }
            else if (pattern[i] == '[') {
                inClass = true;
            }
            else if (pattern[i] == ']') {
                inClass = false;
            }
            else if (pattern[i] == '/' && !inClass) {
                return true;
            }
        }
        return false;
    }

    std::optional<std::string> ReadFile(const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            return std::nullopt;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    std::optional<int> Width(const std::map<std::string, std::string>& properties, const char* key) {
        const auto it = properties.find(key);
        if (it == properties.end()) {
            return std::nullopt;
        }
        const std::optional<long long> value = ParseInteger(it->second);
        if (!value || *value < 1 || *value > 32) {
            return std::nullopt;
        }
        return static_cast<int>(*value);
    }

} // namespace

EditorConfigFile ParseEditorConfig(std::string_view text) {
    EditorConfigFile file;
    std::size_t      lineStart = 0;
    while (lineStart < text.size()) {
        std::size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = text.size();
        }
        const std::string_view line = Trim(text.substr(lineStart, lineEnd - lineStart));
        lineStart                   = lineEnd + 1;

        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            file.sections.push_back(EditorConfigSection{.pattern = std::string(line.substr(1, line.size() - 2))});
            continue;
        }
        const std::size_t equals = line.find_first_of("=:");
        if (equals == std::string_view::npos) {
            continue;
        }
        std::string       key   = Lower(Trim(line.substr(0, equals)));
        const std::string value = std::string(Trim(line.substr(equals + 1)));
        if (key.empty()) {
            continue;
        }
        if (file.sections.empty()) {
            if (key == "root") {
                file.root = Lower(value) == "true";
            }
            continue;
        }
        file.sections.back().properties.emplace_back(std::move(key), value);
    }
    return file;
}

bool EditorConfigGlobMatches(std::string_view pattern, std::string_view relativePath) {
    if (HasSlashOutsideBrackets(pattern)) {
        if (pattern.starts_with('/')) {
            pattern.remove_prefix(1);
        }
        return Match(pattern, relativePath, 0);
    }
    // No '/': the pattern may match at any depth, like a leading "**/".
    for (std::size_t start = 0;;) {
        if (Match(pattern, relativePath.substr(start), 0)) {
            return true;
        }
        const std::size_t slash = relativePath.find('/', start);
        if (slash == std::string_view::npos) {
            return false;
        }
        start = slash + 1;
    }
}

namespace {

    // The shared walk: `fileIn(directory)` is that directory's parsed
    // .editorconfig, or null when it has none.
    template <typename FileIn>
    std::map<std::string, std::string> PropertiesFor(const std::filesystem::path& path, FileIn fileIn) {
        std::error_code             error;
        const std::filesystem::path file = std::filesystem::absolute(path, error).lexically_normal();
        if (error) {
            return {};
        }

        std::vector<std::pair<std::filesystem::path, const EditorConfigFile*>> found; // nearest first
        for (std::filesystem::path directory = file.parent_path();; directory = directory.parent_path()) {
            if (const EditorConfigFile* config = fileIn(directory)) {
                found.emplace_back(directory, config);
                if (config->root) {
                    break;
                }
            }
            if (directory == directory.parent_path()) {
                break;
            }
        }

        std::map<std::string, std::string> properties;
        for (auto it = found.rbegin(); it != found.rend(); ++it) {
            const std::string relative = file.lexically_relative(it->first).generic_string();
            for (const EditorConfigSection& section : it->second->sections) {
                if (!EditorConfigGlobMatches(section.pattern, relative)) {
                    continue;
                }
                for (const auto& [key, value] : section.properties) {
                    std::string lowered = Lower(value);
                    if (lowered == "unset") {
                        properties.erase(key);
                    }
                    else {
                        properties.insert_or_assign(key, std::move(lowered));
                    }
                }
            }
        }
        return properties;
    }

} // namespace

std::map<std::string, std::string> EditorConfigPropertiesFor(const std::filesystem::path& path) {
    std::deque<EditorConfigFile> parsed;
    return PropertiesFor(path, [&parsed](const std::filesystem::path& directory) -> const EditorConfigFile* {
        const std::optional<std::string> text = ReadFile(directory / ".editorconfig");
        return text ? &parsed.emplace_back(ParseEditorConfig(*text)) : nullptr;
    });
}

std::vector<std::optional<text::Charset>> EditorConfigCharsets(std::span<const std::filesystem::path> files) {
    std::vector<std::optional<text::Charset>> charsets(files.size());
    if (!EditorConfigEnabled()) {
        return charsets;
    }
    std::unordered_map<std::string, std::optional<EditorConfigFile>> byDirectory;
    const auto fileIn = [&byDirectory](const std::filesystem::path& directory) -> const EditorConfigFile* {
        auto [it, inserted] = byDirectory.try_emplace(directory.string());
        if (inserted) {
            if (const std::optional<std::string> text = ReadFile(directory / ".editorconfig")) {
                it->second = ParseEditorConfig(*text);
            }
        }
        return it->second ? &*it->second : nullptr;
    };
    for (std::size_t i = 0; i < files.size(); ++i) {
        charsets[i] = EditorConfigConventions(PropertiesFor(files[i], fileIn)).charset;
    }
    return charsets;
}

IndentOverride EditorConfigIndent(const std::map<std::string, std::string>& properties) {
    IndentOverride indent;
    if (const auto style = properties.find("indent_style"); style != properties.end()) {
        if (style->second == "tab") {
            indent.useTabs = true;
        }
        else if (style->second == "space") {
            indent.useTabs = false;
        }
    }
    // indent_size = tab, or tabs with no indent_size, means "a level is one
    // tab stop" -- tab_width's value.
    const auto size = properties.find("indent_size");
    if (size != properties.end() && size->second != "tab") {
        indent.width = Width(properties, "indent_size");
    }
    else if (size != properties.end() || indent.useTabs == true) {
        indent.width = Width(properties, "tab_width");
    }
    return indent;
}

text::FileConventions EditorConfigConventions(const std::map<std::string, std::string>& properties) {
    const auto value = [&properties](const char* key) -> std::string_view {
        const auto it = properties.find(key);
        return it == properties.end() ? std::string_view() : std::string_view(it->second);
    };
    const auto boolean = [&value](const char* key) -> std::optional<bool> {
        if (value(key) == "true") {
            return true;
        }
        if (value(key) == "false") {
            return false;
        }
        return std::nullopt;
    };

    text::FileConventions conventions;
    conventions.ensureFinalNewline     = boolean("insert_final_newline");
    conventions.trimTrailingWhitespace = boolean("trim_trailing_whitespace");
    if (value("end_of_line") == "lf") {
        conventions.lineEnding = text::LineEnding::LF;
    }
    else if (value("end_of_line") == "crlf") {
        conventions.lineEnding = text::LineEnding::CRLF;
    }
    else if (value("end_of_line") == "cr") {
        conventions.lineEnding = text::LineEnding::CR;
    }
    conventions.charset = text::CharsetFromName(value("charset"));
    if (value("max_line_length") == "off") {
        conventions.maxLineLength = 0;
    }
    else if (const std::optional<long long> length = ParseInteger(value("max_line_length")); length && *length > 0) {
        conventions.maxLineLength = static_cast<int>(std::min<long long>(*length, 10000));
    }
    return conventions;
}

void SetEditorConfigEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    EnabledStorage() = enabled;
}

bool EditorConfigEnabled() {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    return EnabledStorage();
}

void ApplyEditorConfigFormatRules(const std::filesystem::path& projectRoot) {
    if (!EditorConfigEnabled()) {
        return;
    }
    const std::map<std::string, std::string> properties = EditorConfigPropertiesFor(projectRoot / "ned.cs");
    // A value may carry an old-style `:severity` suffix.
    const auto value = [&properties](const char* key) -> std::optional<std::string> {
        const auto found = properties.find(key);
        if (found == properties.end()) {
            return std::nullopt;
        }
        return found->second.substr(0, found->second.find(':'));
    };
    const auto boolean = [&](const char* key) -> std::optional<bool> {
        const std::optional<std::string> text = value(key);
        if (text == "true") {
            return true;
        }
        if (text == "false") {
            return false;
        }
        return std::nullopt;
    };
    constexpr auto kLayer = FormatRuleLayer::File;

    if (const std::optional<std::string> braces = value("csharp_new_line_before_open_brace")) {
        // "all", "none", or a list of the constructs whose brace starts its own line.
        const auto listed = [&braces](std::string_view category) {
            if (*braces == "all") {
                return true;
            }
            std::size_t start = 0;
            while (start <= braces->size()) {
                const std::size_t comma = braces->find(',', start);
                std::string_view  item  = std::string_view(*braces).substr(start, comma - start);
                item.remove_prefix(std::min(item.find_first_not_of(' '), item.size()));
                item = item.substr(0, item.find_last_not_of(' ') + 1);
                if (item == category) {
                    return true;
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
            return false;
        };
        const auto placement = [&](std::string_view category) {
            return listed(category) ? BracePlacement::NextLine : BracePlacement::SameLine;
        };
        for (const char* name : {"csharp/brace.class", "csharp/brace.interface", "csharp/brace.namespace"}) {
            SetBracePlacement(name, placement("types"), kLayer);
        }
        SetBracePlacement("csharp/brace.function", placement("methods"), kLayer);
        SetBracePlacement("csharp/brace.control", placement("control_blocks"), kLayer);
    }

    // One capture covers else, catch and finally; only an agreeing set says where it goes.
    const std::optional<bool> beforeElse    = boolean("csharp_new_line_before_else");
    const std::optional<bool> beforeCatch   = boolean("csharp_new_line_before_catch");
    const std::optional<bool> beforeFinally = boolean("csharp_new_line_before_finally");
    if (beforeElse && beforeElse == beforeCatch.value_or(*beforeElse) && beforeElse == beforeFinally.value_or(*beforeElse)) {
        SetBreakBefore("csharp/control.keyword", *beforeElse, kLayer);
    }

    if (const std::optional<bool> spaceAfter = boolean("csharp_space_after_keywords_in_control_flow_statements")) {
        SetSpaceBefore("csharp/control.parens", *spaceAfter, kLayer);
    }
    if (const std::optional<std::string> within = value("csharp_space_between_parentheses")) {
        SetSpaceWithin("csharp/control.parens", within->find("control_flow_statements") != std::string::npos, kLayer);
    }
}

void InstallEditorConfigCharsetResolver() {
    text::SetStatedCharsetResolver([](const std::filesystem::path& path) -> std::optional<text::Charset> {
        if (!EditorConfigEnabled()) {
            return std::nullopt;
        }
        return EditorConfigConventions(EditorConfigPropertiesFor(path)).charset;
    });
}

} // namespace ned::editor
