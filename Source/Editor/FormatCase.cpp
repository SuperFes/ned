#include "FormatCase.h"

namespace ned::editor {

namespace {

    bool IsAsciiLower(char c) {
        return c >= 'a' && c <= 'z';
    }
    bool IsAsciiUpper(char c) {
        return c >= 'A' && c <= 'Z';
    }
    bool IsAsciiAlnum(char c) {
        return IsAsciiLower(c) || IsAsciiUpper(c) || (c >= '0' && c <= '9');
    }
    char ToAsciiLower(char c) {
        return IsAsciiUpper(c) ? static_cast<char>(c - 'A' + 'a') : c;
    }
    char ToAsciiUpper(char c) {
        return IsAsciiLower(c) ? static_cast<char>(c - 'a' + 'A') : c;
    }

    std::string Lower(const std::string& word) {
        std::string result(word);
        for (char& c : result) {
            c = ToAsciiLower(c);
        }
        return result;
    }
    std::string Upper(const std::string& word) {
        std::string result(word);
        for (char& c : result) {
            c = ToAsciiUpper(c);
        }
        return result;
    }
    std::string Capitalize(const std::string& word) {
        std::string result = Lower(word);
        if (!result.empty()) {
            result.front() = ToAsciiUpper(result.front());
        }
        return result;
    }

    // Splits on underscore/hyphen separators AND camelCase-style lower/
    // digit-to-upper transitions ("myVar" -> "my"/"Var") -- a best-effort
    // tokenizer, not a perfect one (an acronym run like "HTMLParser" comes
    // out as "HTML"/"Parser", matching the same imprecision real naming-
    // convention tooling widely accepts rather than trying to solve
    // dictionary-lookup word-splitting). Non-alphanumeric bytes other than
    // the two recognized separators (an operator name's own punctuation,
    // say) simply end whatever word was accumulating rather than being
    // included in it.
    std::vector<std::string> TokenizeWords(std::string_view name) {
        std::vector<std::string> words;
        std::string              current;
        const auto               flush = [&]() {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
        };
        for (const char c : name) {
            if (c == '_' || c == '-') {
                flush();
                continue;
            }
            if (!IsAsciiAlnum(c)) {
                flush();
                continue;
            }
            if (IsAsciiUpper(c) && !current.empty() && IsAsciiLower(current.back())) {
                flush(); // lower -> upper transition: a new camelCase-style word starts here
            }
            current.push_back(c);
        }
        flush();
        return words;
    }

    // The inverse of MatchesCaseConvention's own per-convention rules --
    // joins `words` (already tokenized, original casing discarded) back
    // into one name conforming to `convention`.
    std::string JoinWords(const std::vector<std::string>& words, CaseConvention convention) {
        if (words.empty()) {
            return "";
        }
        switch (convention) {
            case CaseConvention::None: {
                std::string result;
                for (const std::string& word : words) {
                    result += word;
                }
                return result;
            }
            case CaseConvention::Lowercase: {
                std::string result;
                for (const std::string& word : words) {
                    result += Lower(word);
                }
                return result;
            }
            case CaseConvention::Uppercase: {
                std::string result;
                for (const std::string& word : words) {
                    result += Upper(word);
                }
                return result;
            }
            case CaseConvention::CamelCase: {
                std::string result;
                for (std::size_t i = 0; i < words.size(); ++i) {
                    result += (i == 0) ? Lower(words[i]) : Capitalize(words[i]);
                }
                return result;
            }
            case CaseConvention::PascalCase: {
                std::string result;
                for (const std::string& word : words) {
                    result += Capitalize(word);
                }
                return result;
            }
            case CaseConvention::SnakeCase: {
                std::string result;
                for (std::size_t i = 0; i < words.size(); ++i) {
                    if (i != 0) {
                        result += '_';
                    }
                    result += Lower(words[i]);
                }
                return result;
            }
            case CaseConvention::LeadingSnakeCase: {
                std::string result;
                for (std::size_t i = 0; i < words.size(); ++i) {
                    if (i != 0) {
                        result += '_';
                    }
                    result += (i == 0) ? Capitalize(words[i]) : Lower(words[i]);
                }
                return result;
            }
            case CaseConvention::UpperSnakeCase: {
                std::string result;
                for (std::size_t i = 0; i < words.size(); ++i) {
                    if (i != 0) {
                        result += '_';
                    }
                    result += Capitalize(words[i]);
                }
                return result;
            }
            case CaseConvention::ScreamingSnakeCase: {
                std::string result;
                for (std::size_t i = 0; i < words.size(); ++i) {
                    if (i != 0) {
                        result += '_';
                    }
                    result += Upper(words[i]);
                }
                return result;
            }
            case CaseConvention::LispCase: {
                std::string result;
                for (std::size_t i = 0; i < words.size(); ++i) {
                    if (i != 0) {
                        result += '-';
                    }
                    result += Lower(words[i]);
                }
                return result;
            }
        }
        return "";
    }

    // case-catalogue follow-up: derives the naming entity kind straight from
    // marker.definitionKind (the tags query's own capture suffix, e.g.
    // "struct"/"enum_member"/"macro") rather than the coarse SymbolKind
    // bucket -- see FormatCase.h's own header comment on why this file no
    // longer has to conflate class/struct/enum or function/method the way
    // it used to. Three renames below give the tags.scm/ctags vocabulary a
    // more legible entity-kind name than its own raw word; everything else
    // passes through unchanged, so a language that starts emitting a new
    // "@definition.whatever" capture Mode.cpp itself doesn't recognize yet
    // still surfaces here as soon as Mode.cpp does, no FormatCase.cpp change
    // required.
    std::string_view EntityKindForMarker(const SymbolMarker& marker) {
        if (marker.definitionKind == "var" || marker.definitionKind == "variable") {
            // tags.scm's own free-standing-variable vocabulary -- distinct
            // from locals.janet's "local", which only ever covers a
            // function-local binding (see the localScopes branch above).
            return "global";
        }
        if (marker.definitionKind == "property") {
            // Needs real semantics to split further (a getter/setter pair
            // vs. a plain field) -- FormattingCapabilities.md's own C1
            // section. Folding into "field" is honest about that, not a
            // silent conflation: both are "a named piece of data on a type."
            return "field";
        }
        if (marker.definitionKind == "module") {
            return "namespace"; // already folded into SymbolKind::Namespace upstream; same rename here
        }
        if (!marker.definitionKind.empty()) {
            return marker.definitionKind; // class/struct/interface/enum/enum_member/field/constant/macro/function/method/namespace/template_parameter/type, verbatim
        }
        // Defensive fallback for a marker built by hand rather than through
        // SymbolKindFromCaptureName (no real tags-query build does this) --
        // keeps the old coarse-only behavior alive rather than dropping the
        // marker outright.
        switch (marker.kind) {
            case SymbolKind::Callable:
                return "function";
            case SymbolKind::TypeLike:
                return "type";
            case SymbolKind::Namespace:
                return "namespace";
            default:
                return {};
        }
    }

} // namespace

std::string SuggestNameForConvention(std::string_view name, CaseConvention convention) {
    if (MatchesCaseConvention(name, convention)) {
        return std::string(name);
    }
    const std::vector<std::string> words = TokenizeWords(name);
    if (words.empty()) {
        return "";
    }
    return JoinWords(words, convention);
}

std::vector<CaseViolation> ComputeCaseViolations(std::string_view text, std::string_view languageKey,
                                                 const Mode& mode) {
    std::vector<CaseViolation> violations;

    const auto check = [&](std::string_view entityKind, std::string_view name, std::size_t start, std::size_t end) {
        const CaseRuleValue rule = CaseRuleFor(entityKind, languageKey);
        if (!rule.convention || MatchesCaseConvention(name, *rule.convention)) {
            return; // unconfigured, or already conforms -- nothing to report
        }
        violations.push_back(CaseViolation{std::string(entityKind), std::string(name), start, end, *rule.convention,
                                           SuggestNameForConvention(name, *rule.convention)});
    };

    if (mode.localScopes) {
        for (const LocalCapture& capture : mode.localScopes(text)) {
            if (capture.kind != LocalCaptureKind::Definition) {
                continue;
            }
            std::string_view entityKind;
            if (capture.qualifier == "parameter") {
                entityKind = "parameter";
            }
            else if (capture.qualifier == "var") {
                entityKind = "local";
            }
            else {
                continue; // some other language's own qualifier vocabulary -- not this pilot's set
            }
            if (capture.startByte >= capture.endByte || capture.endByte > text.size()) {
                continue; // degenerate span -- never expected from a real query
            }
            check(entityKind, text.substr(capture.startByte, capture.endByte - capture.startByte), capture.startByte,
                 capture.endByte);
        }
    }

    if (mode.symbolKind) {
        for (const SymbolMarker& marker : mode.symbolKind(text)) {
            if (marker.name.empty()) {
                continue; // no "@name" capture in the matched pattern -- nothing to check
            }
            const std::string_view entityKind = EntityKindForMarker(marker);
            if (entityKind.empty()) {
                continue; // Block, or a hand-built marker with no definitionKind at all
            }
            check(entityKind, marker.name, marker.nameStartByte, marker.nameStartByte + marker.name.size());
        }
    }

    return violations;
}

} // namespace ned::editor
