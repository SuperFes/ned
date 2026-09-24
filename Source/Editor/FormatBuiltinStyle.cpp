#include "FormatBuiltinStyle.h"

#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "DataDir.h"
#include "FormatConfigParse.h"
#include "FormatRules.h"

namespace ned::editor {

namespace {

    template <typename Map>
    void RequireUnscopedKeys(const std::filesystem::path& path, const Map& rules) {
        for (const auto& [key, value] : rules) {
            if (key.find('/') != std::string::npos) {
                throw std::runtime_error(path.string() + ": \"" + key +
                                         "\" is already language-scoped -- style.janet keys are scoped to their "
                                         "own language automatically");
            }
        }
    }

    void Validate(const std::filesystem::path& path, const FormatConfig& config) {
        if (!config.indent.empty() || config.trimTrailingWhitespaceOnSave || config.ensureFinalNewline ||
            config.maxConsecutiveBlankLines) {
            throw std::runtime_error(path.string() +
                                     ": style.janet takes only per-capture rule keys (:space :break :blank :wrap "
                                     ":align :arrange :rewrite :case)");
        }
        RequireUnscopedKeys(path, config.space);
        RequireUnscopedKeys(path, config.breakRules);
        RequireUnscopedKeys(path, config.blank);
        RequireUnscopedKeys(path, config.wrap);
        RequireUnscopedKeys(path, config.align);
        RequireUnscopedKeys(path, config.arrange);
        RequireUnscopedKeys(path, config.rewrite);
        RequireUnscopedKeys(path, config.caseRules);
    }

} // namespace

void LoadBuiltinFormatStyles(const std::filesystem::path& languagesDir) {
    std::vector<std::pair<std::string, FormatConfig>> styles;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(languagesDir, ec)) {
        std::error_code isDirEc;
        if (!entry.is_directory(isDirEc)) {
            continue;
        }
        const std::filesystem::path path = entry.path() / "style.janet";
        if (std::optional<FormatConfig> config = ReadFormatConfigFile(path)) {
            Validate(path, *config);
            styles.emplace_back(entry.path().filename().string(), std::move(*config));
        }
    }

    ClearFormatRuleLayer(FormatRuleLayer::Builtin);
    for (const auto& [language, config] : styles) {
        ApplyFormatRules(config, FormatRuleLayer::Builtin, language + "/");
    }
}

void LoadBuiltinFormatStyles() {
    LoadBuiltinFormatStyles(DataDir() / "languages");
}

} // namespace ned::editor
