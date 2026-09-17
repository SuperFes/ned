#include "FormatRewrite.h"

#include <optional>
#include <string>

#include "FormatRules.h"

namespace ned::editor {

namespace {

    char QuoteChar(QuoteStyle style) {
        return style == QuoteStyle::Single ? '\'' : '"';
    }

    // rewrite.quote's own pure delimiter-swap algorithm -- unchanged from
    // the original pilot, just extracted into its own function now that
    // ComputeRewriteEdits hosts a second, structurally unrelated rewrite
    // family alongside it.
    std::optional<FormatTextEdit> TryQuoteSwap(std::string_view text, const FormatCapture& capture,
                                               QuoteStyle style) {
        const std::size_t length = capture.endByte - capture.startByte;
        if (length < 2) {
            return std::nullopt; // not even a well-formed empty-quoted string
        }
        const char open  = text[capture.startByte];
        const char close = text[capture.endByte - 1];
        if (open != '\'' && open != '"') {
            return std::nullopt; // not a quote-delimited node -- template_string never matches this capture at all
        }
        if (open != close) {
            return std::nullopt; // malformed/unterminated -- never expected from a real query, decline rather than guess
        }

        const char target = QuoteChar(style);
        if (open == target) {
            return std::nullopt; // already the desired style
        }

        const std::string_view interior = text.substr(capture.startByte + 1, length - 2);
        for (const char c : interior) {
            if (c == '\\' || c == target) {
                // A backslash means re-escaping this pass doesn't attempt
                // (see this file's own header comment); an unescaped target
                // quote would need one added. Either way: decline.
                return std::nullopt;
            }
        }

        std::string desired;
        desired.reserve(length);
        desired.push_back(target);
        desired.append(interior);
        desired.push_back(target);
        return FormatTextEdit{capture.startByte, capture.endByte, std::move(desired)};
    }

    // rewrite.elseif's own algorithm: a pure keyword-token replacement, only
    // ever applied when the capture spans exactly the literal "elseif" text
    // (verified live against tree-sitter-php's own node-types.json that
    // "elseif" is its own distinct anonymous token, never "else"+"if") --
    // anything else is declined rather than guessed at, the same discipline
    // TryQuoteSwap's own hazard checks already follow.
    std::optional<FormatTextEdit> TryExpandElseif(std::string_view text, const FormatCapture& capture) {
        if (text.substr(capture.startByte, capture.endByte - capture.startByte) != "elseif") {
            return std::nullopt;
        }
        return FormatTextEdit{capture.startByte, capture.endByte, "else if"};
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

        // Each field is its own independent lever -- a capture could in
        // principle be configured for more than one family at once, though
        // in practice a given capture NAME only ever carries one kind of
        // rewrite (rewrite.quote vs. rewrite.elseif).
        if (rule.quoteStyle) {
            if (auto edit = TryQuoteSwap(text, capture, *rule.quoteStyle)) {
                edits.push_back(std::move(*edit));
            }
        }
        if (rule.expandElseif && *rule.expandElseif) {
            if (auto edit = TryExpandElseif(text, capture)) {
                edits.push_back(std::move(*edit));
            }
        }
    }

    return edits;
}

} // namespace ned::editor
