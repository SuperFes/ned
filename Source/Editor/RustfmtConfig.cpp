#include "RustfmtConfig.h"

#include <charconv>
#include <fstream>
#include <sstream>
#include <system_error>

#include "FormatRules.h"

namespace ned::editor {

namespace {

    std::string_view Trim(std::string_view text) {
        const std::size_t start = text.find_first_not_of(" \t\r");
        if (start == std::string_view::npos) {
            return {};
        }
        return text.substr(start, text.find_last_not_of(" \t\r") - start + 1);
    }

    std::optional<int> IntegerValue(std::string_view value) {
        int parsed              = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (error != std::errc() || end != value.data() + value.size()) {
            return std::nullopt;
        }
        return parsed;
    }

    std::optional<bool> BoolValue(std::string_view value) {
        if (value == "true") {
            return true;
        }
        if (value == "false") {
            return false;
        }
        return std::nullopt;
    }

    std::optional<std::string> StringValue(std::string_view value) {
        if (value.size() < 2 || value.front() != value.back() || (value.front() != '"' && value.front() != '\'')) {
            return std::nullopt;
        }
        return std::string(value.substr(1, value.size() - 2));
    }

} // namespace

RustfmtOptions ParseRustfmtToml(std::string_view text) {
    RustfmtOptions options;
    bool           inTable = false;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        std::string_view  line    = text.substr(0, newline);
        text                      = newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);

        if (const std::size_t comment = line.find('#'); comment != std::string_view::npos) {
            line = line.substr(0, comment); // no option value here holds a '#'
        }
        line = Trim(line);
        if (line.starts_with('[')) {
            inTable = true; // everything rustfmt reads is top level
            continue;
        }
        const std::size_t equals = line.find('=');
        if (inTable || equals == std::string_view::npos) {
            continue;
        }
        const std::string_view key   = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        if (key == "hard_tabs") {
            options.hardTabs = BoolValue(value);
        }
        else if (key == "tab_spaces") {
            options.tabSpaces = IntegerValue(value);
        }
        else if (key == "max_width") {
            options.maxWidth = IntegerValue(value);
        }
        else if (key == "brace_style") {
            options.braceStyle = StringValue(value);
        }
        else if (key == "control_brace_style") {
            options.controlBraceStyle = StringValue(value);
        }
        else if (key == "blank_lines_upper_bound") {
            options.blankLinesUpperBound = IntegerValue(value);
        }
    }
    return options;
}

std::optional<std::filesystem::path> FindRustfmtConfig(const std::filesystem::path& directory) {
    std::error_code error;
    for (std::filesystem::path dir = directory; !dir.empty(); dir = dir.parent_path()) {
        for (const char* name : {"rustfmt.toml", ".rustfmt.toml"}) {
            if (std::filesystem::is_regular_file(dir / name, error)) {
                return dir / name;
            }
        }
        if (dir == dir.parent_path()) {
            break;
        }
    }
    return std::nullopt;
}

std::optional<RustfmtOptions> RustfmtOptionsFor(const std::filesystem::path& file) {
    const std::optional<std::filesystem::path> config = FindRustfmtConfig(file.parent_path());
    if (!config) {
        return std::nullopt;
    }
    std::ifstream in(*config, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream content;
    content << in.rdbuf();
    return ParseRustfmtToml(content.str());
}

void ApplyRustfmtRules(const std::filesystem::path& projectRoot) {
    const std::optional<RustfmtOptions> options = RustfmtOptionsFor(projectRoot / "rustfmt.toml");
    if (!options) {
        return;
    }
    constexpr auto kLayer = FormatRuleLayer::File;
    if (options->braceStyle == "AlwaysNextLine") {
        for (const char* name : {"rust/brace.function", "rust/brace.class", "rust/brace.interface", "rust/brace.namespace",
                                 "rust/brace.function.where", "rust/brace.class.where", "rust/brace.interface.where"}) {
            SetBracePlacement(name, BracePlacement::NextLine, kLayer);
        }
    }
    else if (options->braceStyle == "PreferSameLine") {
        for (const char* name : {"rust/brace.function.where", "rust/brace.class.where", "rust/brace.interface.where"}) {
            SetBracePlacement(name, BracePlacement::SameLine, kLayer);
        }
    }
    if (options->controlBraceStyle == "ClosingNextLine" || options->controlBraceStyle == "AlwaysNextLine") {
        SetBreakBefore("rust/control.keyword", true, kLayer);
    }
    if (options->controlBraceStyle == "AlwaysNextLine") {
        SetBracePlacement("rust/brace.control", BracePlacement::NextLine, kLayer);
    }
    if (options->blankLinesUpperBound) {
        SetBlankMaxBefore("rust/def.toplevel", *options->blankLinesUpperBound, kLayer);
        SetBlankMaxBefore("rust/def.method", *options->blankLinesUpperBound, kLayer);
    }
}

} // namespace ned::editor
