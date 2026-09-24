#include "Modeline.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <utility>
#include <vector>

namespace ned::editor {

namespace {

    std::string Lower(std::string_view text) {
        std::string out(text);
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    std::string_view Trim(std::string_view text) {
        const std::size_t start = text.find_first_not_of(" \t");
        if (start == std::string_view::npos) {
            return {};
        }
        return text.substr(start, text.find_last_not_of(" \t") - start + 1);
    }

    std::optional<int> Number(std::string_view text) {
        int value               = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size() || value <= 0 || value > 16) {
            return std::nullopt;
        }
        return value;
    }

    // Vim filetype and Emacs mode spellings that are not ned's own names.
    std::string LanguageName(std::string_view spelled) {
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 14> kAliases{{
            {"c++", "cpp"},
            {"sh", "bash"},
            {"shell-script", "bash"},
            {"js", "javascript"},
            {"js2", "javascript"},
            {"ts", "typescript"},
            {"vlang", "v"},
            {"objective-c", "objc"},
            {"objcpp", "objc"},
            {"yml", "yaml"},
            {"tex", "latex"},
            {"plaintex", "latex"},
            {"makefile", "make"},
            {"conf", "ini"},
        }};
        std::string                                                                    name = Lower(spelled);
        if (name.ends_with("-mode")) {
            name.resize(name.size() - 5); // Emacs's `mode: c++-mode` spelling
        }
        for (const auto& [alias, canonical] : kAliases) {
            if (name == alias) {
                return std::string(canonical);
            }
        }
        return name;
    }

    // `ft=x ts=4 sw=4 et` or `set ft=x ts=4:` -- the options after `vim:`.
    void ReadVimOptions(std::string_view options, Modeline& out) {
        options            = Trim(options);
        const bool setForm = options.starts_with("set ") || options.starts_with("se ");
        if (setForm) {
            options.remove_prefix(options.find(' ') + 1);
            if (const std::size_t colon = options.find(':'); colon != std::string_view::npos) {
                options = options.substr(0, colon); // `set` form ends at the next colon
            }
        }
        std::optional<int> tabstop;
        std::size_t        at = 0;
        while (at < options.size()) {
            const std::size_t      end  = options.find_first_of(setForm ? " \t" : " \t:", at);
            const std::string_view word = options.substr(at, (end == std::string_view::npos ? options.size() : end) - at);
            at                          = end == std::string_view::npos ? options.size() : end + 1;
            if (word.empty()) {
                continue;
            }
            const std::size_t      equals = word.find('=');
            const std::string_view key    = word.substr(0, equals);
            const std::string_view value  = equals == std::string_view::npos ? std::string_view() : word.substr(equals + 1);
            if (key == "ft" || key == "filetype" || key == "syntax" || key == "syn") {
                if (!value.empty()) {
                    out.language = LanguageName(value);
                }
            }
            else if (key == "et" || key == "expandtab") {
                out.useTabs = false;
            }
            else if (key == "noet" || key == "noexpandtab") {
                out.useTabs = true;
            }
            else if (key == "sw" || key == "shiftwidth" || key == "sts" || key == "softtabstop") {
                if (const std::optional<int> width = Number(value)) {
                    out.width = *width;
                }
            }
            else if (key == "ts" || key == "tabstop") {
                tabstop = Number(value);
            }
        }
        if (!out.width && tabstop) {
            out.width = tabstop;
        }
    }

    // `mode: x; tab-width: 4; indent-tabs-mode: nil` or a bare `x`.
    void ReadEmacsVariables(std::string_view variables, Modeline& out) {
        variables = Trim(variables);
        if (variables.find(':') == std::string_view::npos) {
            if (!variables.empty()) {
                out.language = LanguageName(variables);
            }
            return;
        }
        std::optional<int> tabWidth;
        std::size_t        at = 0;
        while (at < variables.size()) {
            const std::size_t      end   = variables.find(';', at);
            const std::string_view entry = variables.substr(at, (end == std::string_view::npos ? variables.size() : end) - at);
            at                           = end == std::string_view::npos ? variables.size() : end + 1;
            const std::size_t colon      = entry.find(':');
            if (colon == std::string_view::npos) {
                continue;
            }
            const std::string      key   = Lower(Trim(entry.substr(0, colon)));
            const std::string_view value = Trim(entry.substr(colon + 1));
            if (key == "mode") {
                out.language = LanguageName(value);
            }
            else if (key == "indent-tabs-mode") {
                out.useTabs = value != "nil";
            }
            else if (key == "tab-width") {
                tabWidth = Number(value);
            }
            else if (key.ends_with("basic-offset") || key.ends_with("indent-offset") || key.ends_with("indent-level")) {
                if (const std::optional<int> width = Number(value)) {
                    out.width = *width;
                }
            }
        }
        if (!out.width && tabWidth) {
            out.width = tabWidth;
        }
    }

    // The first and last kModelineLines lines, in file order.
    std::vector<std::string_view> CandidateLines(std::string_view text) {
        std::vector<std::string_view> lines;
        std::size_t                   at = 0;
        while (at <= text.size()) {
            const std::size_t end = std::min(text.find('\n', at), text.size());
            lines.push_back(text.substr(at, end - at));
            if (end == text.size()) {
                break;
            }
            at = end + 1;
        }
        if (lines.size() <= 2 * kModelineLines) {
            return lines;
        }
        std::vector<std::string_view> kept(lines.begin(), lines.begin() + kModelineLines);
        kept.insert(kept.end(), lines.end() - kModelineLines, lines.end());
        return kept;
    }

} // namespace

Modeline ParseModeline(std::string_view text) {
    Modeline                            out;
    const std::vector<std::string_view> lines = CandidateLines(text);

    // Emacs: the first line, or the second after a `#!`.
    for (std::size_t i = 0; i < std::min<std::size_t>(lines.size(), 2); ++i) {
        const std::string_view line  = lines[i];
        const std::size_t      open  = line.find("-*-");
        const std::size_t      close = open == std::string_view::npos ? open : line.find("-*-", open + 3);
        if (close != std::string_view::npos) {
            ReadEmacsVariables(line.substr(open + 3, close - open - 3), out);
            break;
        }
        if (!line.starts_with("#!")) {
            break;
        }
    }

    // Vim: `vi:`/`vim:`/`ex:` at the start or after whitespace; a later
    // modeline wins, as in Vim.
    for (const std::string_view line : lines) {
        for (const std::string_view marker : {"vim:", "vi:", "ex:"}) {
            const std::size_t found = line.find(marker);
            if (found != std::string_view::npos && (found == 0 || line[found - 1] == ' ' || line[found - 1] == '\t')) {
                ReadVimOptions(line.substr(found + marker.size()), out);
                break;
            }
        }
    }
    return out;
}

} // namespace ned::editor
