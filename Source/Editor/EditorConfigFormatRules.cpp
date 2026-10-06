#include "EditorConfigFormatRules.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "EditorConfig.h"
#include "FormatRules.h"

namespace ned::editor {

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

} // namespace ned::editor
