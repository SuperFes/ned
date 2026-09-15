#include "FormatConfigParse.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#include "FinalNewline.h"
#include "IndentStyle.h"
#include "JanetData.h"
#include "TrimOnSave.h"

namespace ned::editor {

namespace {

    using janetdata::Value;

    [[noreturn]] void Fail(const std::string& path, int line, const std::string& message) {
        throw std::runtime_error(path + ":" + std::to_string(line) + ": " + message);
    }

    bool ExpectBool(const std::string& path, const Value& value, std::string_view what) {
        if (!value.IsBool()) {
            Fail(path, value.line, std::string(what) + " must be true or false");
        }
        return value.boolean;
    }

    // JanetData.h's reader has no dedicated integer Kind -- a bare number
    // reads as a Symbol (see JanetData.cpp's ReadValue: anything that isn't
    // true/false/nil/a keyword/a string/a bracketed form falls through to
    // Symbol). Validated here rather than in the shared reader, which is
    // deliberately kept to its existing small accepted subset.
    int ExpectInt(const std::string& path, const Value& value, std::string_view what) {
        if (value.kind != Value::Kind::Symbol || value.text.empty()) {
            Fail(path, value.line, std::string(what) + " must be an integer");
        }
        std::size_t i = 0;
        if (value.text[0] == '-') {
            i = 1;
        }
        if (i >= value.text.size()) {
            Fail(path, value.line, std::string(what) + " must be an integer");
        }
        for (; i < value.text.size(); ++i) {
            if (value.text[i] < '0' || value.text[i] > '9') {
                Fail(path, value.line, std::string(what) + " must be an integer");
            }
        }
        return std::stoi(value.text);
    }

} // namespace

FormatConfig ParseFormatConfig(std::string_view source, const std::string& path) {
    Value root;
    try {
        root = janetdata::ParseJanetData(source);
    }
    catch (const janetdata::JanetDataError& error) {
        throw std::runtime_error(path + ":" + error.what());
    }
    if (!root.IsStruct()) {
        Fail(path, root.line, "a format config is one {:key value ...} struct");
    }

    FormatConfig config;

    for (std::size_t i = 0; i + 1 < root.pairs.size(); i += 2) {
        const Value& keyValue = root.pairs[i];
        const Value& value    = root.pairs[i + 1];
        if (!keyValue.IsKeyword()) {
            Fail(path, keyValue.line, "format config keys are keywords (:indent, :trim-trailing-whitespace, ...)");
        }
        const std::string& key = keyValue.text;

        if (key == "indent") {
            if (!value.IsStruct()) {
                Fail(path, value.line, ":indent is {:<language-key> {:tabs true/false :width N} ...}");
            }
            for (std::size_t j = 0; j + 1 < value.pairs.size(); j += 2) {
                const Value& languageKey = value.pairs[j];
                const Value& entryValue  = value.pairs[j + 1];
                if (!languageKey.IsKeyword()) {
                    Fail(path, languageKey.line, ":indent's own keys are language keys (:python, :cpp, ...)");
                }
                if (!entryValue.IsStruct()) {
                    Fail(path, entryValue.line, ":indent's " + languageKey.text + " entry must be {:tabs true/false :width N}");
                }
                FormatConfigIndentEntry entry;
                for (std::size_t k = 0; k + 1 < entryValue.pairs.size(); k += 2) {
                    const Value& fieldKey   = entryValue.pairs[k];
                    const Value& fieldValue = entryValue.pairs[k + 1];
                    if (!fieldKey.IsKeyword()) {
                        Fail(path, fieldKey.line, ":indent entries are keyed by :tabs/:width");
                    }
                    if (fieldKey.text == "tabs") {
                        entry.useTabs = ExpectBool(path, fieldValue, "\"" + languageKey.text + "\"'s :tabs");
                    }
                    else if (fieldKey.text == "width") {
                        entry.width = ExpectInt(path, fieldValue, "\"" + languageKey.text + "\"'s :width");
                    }
                    else {
                        Fail(path, fieldKey.line, "unknown :indent entry key :" + fieldKey.text);
                    }
                }
                config.indent[languageKey.text] = entry;
            }
        }
        else if (key == "trim-trailing-whitespace") {
            config.trimTrailingWhitespaceOnSave = ExpectBool(path, value, ":trim-trailing-whitespace");
        }
        else if (key == "ensure-final-newline") {
            config.ensureFinalNewline = ExpectBool(path, value, ":ensure-final-newline");
        }
        else {
            Fail(path, keyValue.line, "unknown format config key :" + key);
        }
    }

    return config;
}

void ApplyFormatConfig(const FormatConfig& config) {
    for (const auto& [languageKey, entry] : config.indent) {
        const std::string modeName = languageKey + "-mode"; // the inverse of imprint::LanguageKeyForMode
        IndentStyle       style    = EffectiveIndentStyle(modeName);
        if (entry.useTabs) {
            style.useTabs = *entry.useTabs;
        }
        if (entry.width) {
            style.width = *entry.width;
        }
        SetIndentStyleForMode(modeName, style);
    }
    if (config.trimTrailingWhitespaceOnSave) {
        SetTrimTrailingWhitespaceOnSave(*config.trimTrailingWhitespaceOnSave);
    }
    if (config.ensureFinalNewline) {
        SetEnsureFinalNewline(*config.ensureFinalNewline);
    }
}

std::filesystem::path PersonalFormatConfigPath() {
    if (const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME"); xdgConfigHome && *xdgConfigHome) {
        return std::filesystem::path(xdgConfigHome) / "ned" / "format.janet";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config" / "ned" / "format.janet";
    }
    throw std::runtime_error("ned: cannot determine config directory (neither XDG_CONFIG_HOME nor HOME is set)");
}

std::filesystem::path ProjectFormatConfigPath(const std::filesystem::path& projectRoot) {
    return projectRoot / ".ned" / "format.janet";
}

void LoadFormatConfigFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return; // no config there -- not an error
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return; // exists but unreadable (permissions, race) -- treated the same as absent
    }
    std::ostringstream content;
    content << in.rdbuf();
    ApplyFormatConfig(ParseFormatConfig(content.str(), path.string()));
}

} // namespace ned::editor
