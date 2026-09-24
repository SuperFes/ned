#include "PrettierConfig.h"

#include <array>
#include <charconv>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

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

    std::string_view Unquoted(std::string_view value) {
        if (value.size() >= 2 && value.front() == value.back() && (value.front() == '"' || value.front() == '\'')) {
            return value.substr(1, value.size() - 2);
        }
        return value;
    }

    // Only a bare integer and true/false are read; anything else is skipped.
    void Assign(PrettierOptions& options, std::string_view key, std::string_view value) {
        const auto integer = [&]() -> std::optional<int> {
            int parsed              = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            return error == std::errc() && end == value.data() + value.size() ? std::optional<int>(parsed) : std::nullopt;
        };
        const auto boolean = [&]() -> std::optional<bool> {
            if (value == "true") {
                return true;
            }
            if (value == "false") {
                return false;
            }
            return std::nullopt;
        };
        if (key == "useTabs") {
            options.useTabs = boolean();
        }
        else if (key == "tabWidth") {
            options.tabWidth = integer();
        }
        else if (key == "printWidth") {
            options.printWidth = integer();
        }
        else if (key == "singleQuote") {
            options.singleQuote = boolean();
        }
    }

    PrettierOptions FromJson(const nlohmann::json& object) {
        PrettierOptions options;
        if (!object.is_object()) {
            return options;
        }
        for (const auto& [key, value] : object.items()) {
            if (value.is_boolean() || value.is_number_integer()) {
                Assign(options, key, value.dump());
            }
        }
        return options;
    }

    std::optional<std::string> ReadFile(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        std::ostringstream content;
        content << in.rdbuf();
        return content.str();
    }

    // Prettier's own search order within one directory.
    constexpr std::array kReadable{".prettierrc", ".prettierrc.json", ".prettierrc.yaml", ".prettierrc.yml",
                                   ".prettierrc.json5", ".prettierrc.toml"};
    constexpr std::array kUnreadable{".prettierrc.js", ".prettierrc.cjs", ".prettierrc.mjs", ".prettierrc.ts",
                                     "prettier.config.js", "prettier.config.cjs", "prettier.config.mjs",
                                     "prettier.config.ts"};

    // The file extensions Prettier formats out of the box.
    constexpr std::array kFormatted{".js", ".jsx", ".mjs", ".cjs", ".ts", ".tsx", ".mts", ".cts", ".json", ".json5",
                                    ".css", ".scss", ".less", ".html", ".vue", ".md", ".markdown", ".yaml", ".yml",
                                    ".graphql", ".gql"};

} // namespace

std::optional<PrettierOptions> ParsePrettierConfig(std::string_view text) {
    const std::string_view trimmed = Trim(text);
    if (trimmed.starts_with('{')) {
        const nlohmann::json root = nlohmann::json::parse(trimmed, nullptr, /*allow_exceptions=*/false,
                                                          /*ignore_comments=*/true);
        if (root.is_discarded()) {
            return std::nullopt;
        }
        return FromJson(root);
    }
    PrettierOptions options;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        std::string_view  line    = text.substr(0, newline);
        text                      = newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);
        if (const std::size_t comment = line.find('#'); comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        if (line.starts_with(' ') || line.starts_with('\t')) {
            continue; // nested under `overrides:` or a table -- only top-level options apply
        }
        line                    = Trim(line);
        const std::size_t colon = line.find_first_of(":=");
        if (line.starts_with('[') || colon == std::string_view::npos) {
            continue;
        }
        Assign(options, Unquoted(Trim(line.substr(0, colon))), Unquoted(Trim(line.substr(colon + 1))));
    }
    return options;
}

std::optional<PrettierOptions> PrettierOptionsIn(const std::filesystem::path& directory) {
    std::error_code error;
    for (std::filesystem::path dir = directory; !dir.empty(); dir = dir.parent_path()) {
        if (const std::filesystem::path package = dir / "package.json"; std::filesystem::is_regular_file(package, error)) {
            if (const std::optional<std::string> text = ReadFile(package)) {
                const nlohmann::json root = nlohmann::json::parse(*text, nullptr, false);
                if (root.is_object() && root.contains("prettier")) {
                    // A string names a shared config package, which only Node can resolve.
                    return root["prettier"].is_object() ? std::optional(FromJson(root["prettier"])) : std::nullopt;
                }
            }
        }
        for (const char* name : kReadable) {
            if (std::filesystem::is_regular_file(dir / name, error)) {
                const std::optional<std::string> text = ReadFile(dir / name);
                return text ? ParsePrettierConfig(*text) : std::nullopt;
            }
        }
        for (const char* name : kUnreadable) {
            if (std::filesystem::is_regular_file(dir / name, error)) {
                return std::nullopt;
            }
        }
        if (dir == dir.parent_path()) {
            break;
        }
    }
    return std::nullopt;
}

std::optional<PrettierOptions> PrettierOptionsFor(const std::filesystem::path& file) {
    const std::string extension = file.extension().string();
    for (const char* formatted : kFormatted) {
        if (extension == formatted) {
            return PrettierOptionsIn(file.parent_path());
        }
    }
    return std::nullopt;
}

void ApplyPrettierRules(const std::filesystem::path& projectRoot) {
    const std::optional<PrettierOptions> options = PrettierOptionsIn(projectRoot);
    if (!options) {
        return;
    }
    const QuoteStyle quotes = options->singleQuote.value_or(false) ? QuoteStyle::Single : QuoteStyle::Double;
    for (const char* name : {"javascript/rewrite.quote", "typescript/rewrite.quote", "tsx/rewrite.quote"}) {
        SetRewriteQuoteStyle(name, quotes, FormatRuleLayer::File);
    }
}

} // namespace ned::editor
