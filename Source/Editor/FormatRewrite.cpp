#include "FormatRewrite.h"

#include "FormatRules.h"

namespace ned::editor {

namespace {

    char QuoteChar(QuoteStyle style) {
        return style == QuoteStyle::Single ? '\'' : '"';
    }

} // namespace

std::vector<FormatTextEdit> ComputeRewriteEdits(std::string_view text, std::string_view languageKey,
                                                const std::vector<FormatCapture>& captures) {
    std::vector<FormatTextEdit> edits;

    for (const FormatCapture& capture : captures) {
        if (capture.startByte >= capture.endByte || capture.endByte > text.size()) {
            continue; // degenerate span -- never expected from a real query
        }
        const RewriteRuleValue rule = RewriteRuleFor(capture.name, languageKey);
        if (!rule.quoteStyle) {
            continue; // unconfigured -- no built-in default, nothing forced
        }

        const std::size_t length = capture.endByte - capture.startByte;
        if (length < 2) {
            continue; // not even a well-formed empty-quoted string
        }
        const char open  = text[capture.startByte];
        const char close = text[capture.endByte - 1];
        if (open != '\'' && open != '"') {
            continue; // not a quote-delimited node -- template_string never matches this capture at all
        }
        if (open != close) {
            continue; // malformed/unterminated -- never expected from a real query, decline rather than guess
        }

        const char target = QuoteChar(*rule.quoteStyle);
        if (open == target) {
            continue; // already the desired style
        }

        const std::string_view interior = text.substr(capture.startByte + 1, length - 2);
        bool                   safe     = true;
        for (const char c : interior) {
            if (c == '\\' || c == target) {
                // A backslash means re-escaping this pass doesn't attempt
                // (see this file's own header comment); an unescaped target
                // quote would need one added. Either way: decline.
                safe = false;
                break;
            }
        }
        if (!safe) {
            continue;
        }

        std::string desired;
        desired.reserve(length);
        desired.push_back(target);
        desired.append(interior);
        desired.push_back(target);
        edits.push_back(FormatTextEdit{capture.startByte, capture.endByte, std::move(desired)});
    }

    return edits;
}

} // namespace ned::editor
