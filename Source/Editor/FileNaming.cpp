#include "FileNaming.h"

#include <mutex>
#include <unordered_map>

#include "Project/Root.h"

namespace ned::editor {

namespace {

    std::mutex& RulesMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::unordered_map<std::string, FileNamingRuleValue>& Rules() {
        static std::unordered_map<std::string, FileNamingRuleValue> rules;
        return rules;
    }

    bool& AutoHeaderGuardFlag() {
        static bool enabled = false;
        return enabled;
    }

    // ROADMAP.md's own literal example, for the two languages that actually
    // have a header/source split (Editor/HeaderSource.h's own header
    // comment: "C/C++ ... only: no other bundled language has an
    // equivalent split-file convention").
    const std::unordered_map<std::string, std::string>& BuiltInHeaderGuardTemplates() {
        static const std::unordered_map<std::string, std::string> templates = {
            {"cpp", "${PROJECT_NAME}_${FILE_NAME}_${EXT}"},
            {"c", "${PROJECT_NAME}_${FILE_NAME}_${EXT}"},
        };
        return templates;
    }

    bool IsAsciiAlnum(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }

    char ToAsciiUpper(char c) {
        return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }

    // Uppercases, then collapses every run of non-alnum bytes to a single
    // '_' and trims a leading/trailing one -- "my-project.v2" ->
    // "MY_PROJECT_V2", the shape a preprocessor identifier needs. Empty
    // input (an empty project/file name) stays empty; the caller substitutes
    // a fallback for that case rather than this function guessing one.
    std::string SanitizeForMacro(std::string_view text) {
        std::string result;
        result.reserve(text.size());
        bool pendingUnderscore = false;
        for (const char c : text) {
            if (IsAsciiAlnum(c)) {
                if (pendingUnderscore && !result.empty()) {
                    result.push_back('_');
                }
                pendingUnderscore = false;
                result.push_back(ToAsciiUpper(c));
            }
            else {
                pendingUnderscore = true;
            }
        }
        return result;
    }

    bool ReplaceAll(std::string& text, std::string_view placeholder, std::string_view replacement) {
        bool   replacedAny = false;
        std::size_t pos    = 0;
        while ((pos = text.find(placeholder, pos)) != std::string::npos) {
            text.replace(pos, placeholder.size(), replacement);
            pos += replacement.size();
            replacedAny = true;
        }
        return replacedAny;
    }

} // namespace

void SetFileNamingCaseConvention(const std::string& language, std::optional<CaseConvention> value) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    Rules()[language].caseConvention = value;
}

void SetHeaderGuardTemplate(const std::string& language, std::optional<std::string> value) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    Rules()[language].headerGuardTemplate = std::move(value);
}

void SetAutoHeaderGuard(bool enabled) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    AutoHeaderGuardFlag()                = enabled;
}

bool AutoHeaderGuardEnabled() {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    return AutoHeaderGuardFlag();
}

FileNamingRuleValue FileNamingRuleFor(std::string_view language) {
    const std::lock_guard<std::mutex> lock(RulesMutex());
    const std::string                 key = std::string(language);
    FileNamingRuleValue                value;
    if (const auto it = Rules().find(key); it != Rules().end()) {
        value = it->second;
    }
    if (!value.headerGuardTemplate) {
        // Nothing ever configured (SetHeaderGuardTemplate was never called
        // for this language at all) -- fall back to the built-in default,
        // if this language has one. An explicit SetHeaderGuardTemplate(lang,
        // "") already left value.headerGuardTemplate holding an empty
        // string, which the `!value.headerGuardTemplate` check above treats
        // as "configured" (a real value, just an empty one), so it's never
        // overwritten here -- that's the mechanism by which a caller turns a
        // built-in default off.
        if (const auto it = Rules().find(key); it == Rules().end() || !it->second.headerGuardTemplate) {
            if (const auto builtin = BuiltInHeaderGuardTemplates().find(key); builtin != BuiltInHeaderGuardTemplates().end()) {
                value.headerGuardTemplate = builtin->second;
            }
        }
    }
    return value;
}

std::string ExpandHeaderGuardTemplate(std::string_view templateText, const std::filesystem::path& path) {
    std::string result(templateText);

    std::string projectName = ProjectRoot().filename().string();
    if (projectName.empty()) {
        projectName = ProjectRoot().string();
    }
    std::string fileName = path.stem().string();
    std::string extension = path.extension().string();
    if (!extension.empty() && extension.front() == '.') {
        extension.erase(extension.begin());
    }

    ReplaceAll(result, "${PROJECT_NAME}", SanitizeForMacro(projectName.empty() ? "PROJECT" : projectName));
    ReplaceAll(result, "${FILE_NAME}", SanitizeForMacro(fileName));
    ReplaceAll(result, "${EXT}", SanitizeForMacro(extension));
    return result;
}

} // namespace ned::editor
