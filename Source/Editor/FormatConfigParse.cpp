#include "FormatConfigParse.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#include "FinalNewline.h"
#include "FormatRules.h"
#include "IndentStyle.h"
#include "JanetData.h"
#include "MaxConsecutiveBlankLines.h"
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

    std::string ExpectString(const std::string& path, const Value& value, std::string_view what) {
        if (!value.IsString()) {
            Fail(path, value.line, std::string(what) + " must be a string");
        }
        return value.text;
    }

    BracePlacement ExpectBracePlacement(const std::string& path, const Value& value, std::string_view what) {
        if (!value.IsKeyword()) {
            Fail(path, value.line,
                std::string(what) + " must be a keyword (:same-line, :next-line, or :next-line-indented)");
        }
        try {
            return BracePlacementByName(value.text);
        }
        catch (const std::runtime_error&) {
            Fail(path, value.line, std::string(what) + " must be :same-line, :next-line, or :next-line-indented");
        }
    }

    // :space's own {"<capture>" {:before true/false :after true/false
    // :within true/false} ...} -- the capture-name key is a STRING (a
    // dotted capture name isn't a valid Janet keyword symbol), unlike every
    // other struct key in this file.
    SpaceRuleValue ParseSpaceEntry(const std::string& path, const std::string& captureKey, const Value& entryValue) {
        if (!entryValue.IsStruct()) {
            Fail(path, entryValue.line, "\"" + captureKey + "\"'s :space entry must be {:before .. :after .. :within ..}");
        }
        SpaceRuleValue entry;
        for (std::size_t k = 0; k + 1 < entryValue.pairs.size(); k += 2) {
            const Value& fieldKey   = entryValue.pairs[k];
            const Value& fieldValue = entryValue.pairs[k + 1];
            if (!fieldKey.IsKeyword()) {
                Fail(path, fieldKey.line, ":space entries are keyed by :before/:after/:within");
            }
            if (fieldKey.text == "before") {
                entry.before = ExpectBool(path, fieldValue, "\"" + captureKey + "\"'s :before");
            }
            else if (fieldKey.text == "after") {
                entry.after = ExpectBool(path, fieldValue, "\"" + captureKey + "\"'s :after");
            }
            else if (fieldKey.text == "within") {
                entry.within = ExpectBool(path, fieldValue, "\"" + captureKey + "\"'s :within");
            }
            else {
                Fail(path, fieldKey.line, "unknown :space entry key :" + fieldKey.text);
            }
        }
        return entry;
    }

    // :break's own entry -- same shape one level up, with brace placement
    // folded in (Editor/FormatRules.h's own header comment explains why).
    BreakRuleValue ParseBreakEntry(const std::string& path, const std::string& captureKey, const Value& entryValue) {
        if (!entryValue.IsStruct()) {
            Fail(path, entryValue.line,
                "\"" + captureKey +
                    "\"'s :break entry must be {:before .. :after .. :placement .. :collapse-empty .. "
                    ":collapse-simple ..}");
        }
        BreakRuleValue entry;
        for (std::size_t k = 0; k + 1 < entryValue.pairs.size(); k += 2) {
            const Value& fieldKey   = entryValue.pairs[k];
            const Value& fieldValue = entryValue.pairs[k + 1];
            if (!fieldKey.IsKeyword()) {
                Fail(path, fieldKey.line,
                    ":break entries are keyed by :before/:after/:placement/:collapse-empty/:collapse-simple");
            }
            if (fieldKey.text == "before") {
                entry.before = ExpectBool(path, fieldValue, "\"" + captureKey + "\"'s :before");
            }
            else if (fieldKey.text == "after") {
                entry.after = ExpectBool(path, fieldValue, "\"" + captureKey + "\"'s :after");
            }
            else if (fieldKey.text == "placement") {
                entry.placement = ExpectBracePlacement(path, fieldValue, "\"" + captureKey + "\"'s :placement");
            }
            else if (fieldKey.text == "collapse-empty") {
                entry.collapseEmpty = ExpectBool(path, fieldValue, "\"" + captureKey + "\"'s :collapse-empty");
            }
            else if (fieldKey.text == "collapse-simple") {
                entry.collapseSimple = ExpectBool(path, fieldValue, "\"" + captureKey + "\"'s :collapse-simple");
            }
            else {
                Fail(path, fieldKey.line, "unknown :break entry key :" + fieldKey.text);
            }
        }
        return entry;
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
        else if (key == "space") {
            if (!value.IsStruct()) {
                Fail(path, value.line, ":space is {\"<capture>\" {:before .. :after .. :within ..} ...}");
            }
            for (std::size_t j = 0; j + 1 < value.pairs.size(); j += 2) {
                const Value& captureKey = value.pairs[j];
                const Value& entryValue = value.pairs[j + 1];
                const std::string capture =
                    ExpectString(path, captureKey, ":space's own keys are capture-name strings, e.g. \"control.parens\"");
                config.space[capture] = ParseSpaceEntry(path, capture, entryValue);
            }
        }
        else if (key == "break") {
            if (!value.IsStruct()) {
                Fail(path, value.line, ":break is {\"<capture>\" {:before .. :after .. :placement ..} ...}");
            }
            for (std::size_t j = 0; j + 1 < value.pairs.size(); j += 2) {
                const Value& captureKey = value.pairs[j];
                const Value& entryValue = value.pairs[j + 1];
                const std::string capture =
                    ExpectString(path, captureKey, ":break's own keys are capture-name strings, e.g. \"brace.function\"");
                config.breakRules[capture] = ParseBreakEntry(path, capture, entryValue);
            }
        }
        else if (key == "trim-trailing-whitespace") {
            config.trimTrailingWhitespaceOnSave = ExpectBool(path, value, ":trim-trailing-whitespace");
        }
        else if (key == "ensure-final-newline") {
            config.ensureFinalNewline = ExpectBool(path, value, ":ensure-final-newline");
        }
        else if (key == "max-consecutive-blank-lines") {
            config.maxConsecutiveBlankLines = ExpectInt(path, value, ":max-consecutive-blank-lines");
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
    for (const auto& [captureKey, entry] : config.space) {
        if (entry.before) {
            SetSpaceBefore(captureKey, entry.before);
        }
        if (entry.after) {
            SetSpaceAfter(captureKey, entry.after);
        }
        if (entry.within) {
            SetSpaceWithin(captureKey, entry.within);
        }
    }
    for (const auto& [captureKey, entry] : config.breakRules) {
        if (entry.before) {
            SetBreakBefore(captureKey, entry.before);
        }
        if (entry.after) {
            SetBreakAfter(captureKey, entry.after);
        }
        if (entry.placement) {
            SetBracePlacement(captureKey, entry.placement);
        }
        if (entry.collapseEmpty) {
            SetBraceCollapseEmpty(captureKey, entry.collapseEmpty);
        }
        if (entry.collapseSimple) {
            SetBraceCollapseSimple(captureKey, entry.collapseSimple);
        }
    }
    if (config.trimTrailingWhitespaceOnSave) {
        SetTrimTrailingWhitespaceOnSave(*config.trimTrailingWhitespaceOnSave);
    }
    if (config.ensureFinalNewline) {
        SetEnsureFinalNewline(*config.ensureFinalNewline);
    }
    if (config.maxConsecutiveBlankLines) {
        SetMaxConsecutiveBlankLines(*config.maxConsecutiveBlankLines);
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

std::vector<std::string> FormatConfigKeys() {
    return {"break", "ensure-final-newline", "indent", "max-consecutive-blank-lines", "space", "trim-trailing-whitespace"};
}

std::vector<std::string> FormatConfigIndentEntryKeys() {
    return {"tabs", "width"};
}

std::vector<std::string> FormatConfigSpaceEntryKeys() {
    return {"after", "before", "within"};
}

std::vector<std::string> FormatConfigBreakEntryKeys() {
    return {"after", "before", "collapse-empty", "collapse-simple", "placement"};
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
