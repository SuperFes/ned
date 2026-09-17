#include "FormatWrap.h"

#include "FormatRules.h"
#include "Indent.h"
#include "IndentStyle.h"

namespace ned::editor {

namespace {

    // "Never": every item joined by ", " on the header's own line -- no
    // per-item indent, no trailing separator (a collapsed single-line list
    // gets no trailing comma regardless of :force-trailing-comma, matching
    // ordinary call-site style; that field is only meaningful for a
    // multi-line result, per WrapRuleValue's own header comment).
    std::string CollapsedInterior(std::string_view text, const std::vector<std::pair<std::size_t, std::size_t>>& items) {
        std::string desired;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i != 0) {
                desired += ", ";
            }
            const auto [itemStart, itemEnd] = items[i];
            desired.append(text.substr(itemStart, itemEnd - itemStart));
        }
        return desired;
    }

    // "Always": one item per line, indented one level past the header's own
    // line, the closer landing on its own line back at the header's own
    // indent -- the same "closer aligns with the header" convention every
    // other NextLine-shaped placement in this codebase already uses. The
    // returned string is everything between the open delimiter's own end
    // and the close delimiter's own start, INCLUDING the leading/trailing
    // newlines, so the close token itself never needs to move -- only the
    // text before it changes.
    std::string ChoppedInterior(std::string_view text, const std::vector<std::pair<std::size_t, std::size_t>>& items,
                                std::string_view headerIndent, bool forceTrailingComma, const IndentStyle& style) {
        const std::string itemIndent = std::string(headerIndent) + IndentString(style.width, style);
        std::string        desired   = "\n";
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto [itemStart, itemEnd] = items[i];
            desired += itemIndent;
            desired.append(text.substr(itemStart, itemEnd - itemStart));
            const bool isLast = (i + 1 == items.size());
            if (!isLast || forceTrailingComma) {
                desired += ',';
            }
            desired += '\n';
        }
        desired += headerIndent;
        return desired;
    }

    // A real corruption hazard found live, not by inspection: a trailing
    // comma after a CALL's own last argument is a hard syntax error in C++
    // (confirmed with a real `g++` compile -- "expected primary-expression
    // before ')' token"), unlike a brace-init-list ("{1, 2, 3,}", also
    // confirmed live), where JetBrains' own "force trailing comma if
    // multiline" feature is squarely aimed. This is a genuinely
    // per-language, per-construct fact -- ES2017+ JavaScript, for one,
    // legally allows a trailing comma in a call's own argument list -- so
    // this declines only what's actually been verified unsafe rather than
    // a blanket "wrap.args never gets a trailing comma" rule that would be
    // wrong the moment a second language's own "wrap.args" is added. The
    // chop-down LAYOUT itself (one item per line, no trailing comma) is
    // unaffected and stays fully available -- confirmed live it compiles
    // clean -- only the companion boolean is declined here.
    //
    // wrap.params gets its OWN entry, not a reuse of wrap.args's: a
    // trailing comma after a function's last parameter is a separately
    // confirmed C++ syntax error (a real `g++` compile: "expected
    // identifier before ')' token") -- a different grammar construct, not
    // something safe to assume carries over from the call-argument case.
    bool TrailingCommaUnsafeForLanguage(std::string_view languageKey, std::string_view captureName) {
        return languageKey == "cpp" && (captureName == "wrap.args" || captureName == "wrap.params");
    }

} // namespace

std::vector<FormatTextEdit> ComputeWrapEdits(std::string_view text, std::string_view languageKey,
                                             const std::vector<FormatCapture>& captures) {
    std::vector<FormatTextEdit> edits;
    const IndentStyle           style = EffectiveIndentStyle(std::string(languageKey) + "-mode");

    for (const FormatCapture& capture : captures) {
        if (capture.items.empty()) {
            continue; // nothing to wrap -- and a plain non-list capture never gets .item captures at all
        }
        WrapRuleValue rule = WrapRuleFor(capture.name, languageKey);
        if (!rule.policy) {
            continue; // unconfigured -- no built-in default, nothing forced
        }
        if (rule.forceTrailingComma && TrailingCommaUnsafeForLanguage(languageKey, capture.name)) {
            rule.forceTrailingComma.reset(); // see TrailingCommaUnsafeForLanguage's own comment
        }
        if (capture.startByte == 0 || capture.startByte >= capture.endByte || capture.endByte > text.size()) {
            continue; // degenerate span -- never expected from a real query
        }
        if (capture.openLength == 0 || capture.closeLength == 0 ||
            capture.openLength + capture.closeLength > capture.endByte - capture.startByte) {
            continue; // degenerate delimiter lengths -- never expected from a real .open/.close pair
        }

        const std::size_t interiorStart = capture.startByte + capture.openLength;
        const std::size_t interiorEnd   = capture.endByte - capture.closeLength;
        if (interiorStart > interiorEnd) {
            continue;
        }

        std::string desired;
        if (*rule.policy == WrapPolicy::Never) {
            desired = CollapsedInterior(text, capture.items);
        }
        else {
            const std::string_view headerIndent = LineIndentOf(text, capture.startByte);
            desired = ChoppedInterior(text, capture.items, headerIndent,
                                      rule.forceTrailingComma && *rule.forceTrailingComma, style);
        }

        const std::string_view current = text.substr(interiorStart, interiorEnd - interiorStart);
        if (current != desired) {
            edits.push_back(FormatTextEdit{interiorStart, interiorEnd, std::move(desired)});
        }
    }

    return edits;
}

} // namespace ned::editor
