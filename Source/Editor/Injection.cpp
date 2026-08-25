#include "Injection.h"

#include <algorithm>
#include <cctype>

#include "ModeOverrides.h"
#include "TreeSitter/Languages.h"
#include "TreeSitter/Queries.h"

namespace ned::editor {

namespace {

    // Common shorthand tags people actually type (a Markdown fence tag, an
    // HTML/injections.scm #set! value, ...), mapped to Ned's canonical
    // treesitter::LanguageByName/ModeByName spelling. A tag not listed here
    // passes through unchanged (lowercased), so exact canonical names
    // (python, html, ...) work with no entry.
    std::string CanonicalEmbeddedLanguageName(std::string_view tag) {
        static const std::unordered_map<std::string, std::string> kAliases = {
            {"js", "javascript"},
            {"jsx", "tsx"},
            {"ts", "typescript"},
            {"py", "python"},
            {"sh", "bash"},
            {"shell", "bash"},
            {"zsh", "bash"},
            {"c++", "cpp"},
            {"cc", "cpp"},
            {"cxx", "cpp"},
            {"hpp", "cpp"},
            {"h", "c"},
            {"yml", "yaml"},
            {"clj", "clojure"},
            // Upstream injections.scm files spell markdown's inline grammar
            // with an underscore (Neovim's own parser-name convention);
            // Ned's own treesitter::LanguageByName registers it with a
            // hyphen (TreeSitter/Languages.cpp) since it isn't a real
            // filetype name.
            {"markdown_inline", "markdown-inline"},
        };
        std::string lower(tag);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto it = kAliases.find(lower);
        return it != kAliases.end() ? it->second : lower;
    }

    // Tier-2 resolution: a highlighting-only sub-grammar with no real
    // ModeByName-reachable Mode. Currently just markdown-inline, built
    // through the same generic TreeSitterModeFromLanguage(...) machinery
    // every bundled Mode's own highlight closure goes through, rather than
    // duplicating capture-to-SyntaxClass mapping here. The extra appended
    // "(strikethrough)" pattern mirrors MarkdownMode()'s own addition (see
    // Mode.cpp's CaptureTable() "text.strikethrough" doc comment) -- the
    // grammar's own highlights.scm doesn't capture it, and this is the only
    // other place that runs this grammar's highlighting.
    std::optional<HighlightFunction> BuildGrammarOnlyHighlight(const std::string& canonicalName) {
        if (canonicalName != "markdown-inline") {
            return std::nullopt;
        }
        const auto language = treesitter::LanguageByName("markdown-inline");
        if (!language) {
            return std::nullopt;
        }
        const std::string querySource =
            std::string(treesitter::queries::kMarkdownInline) + "\n(strikethrough) @text.strikethrough\n";
        Mode mode = TreeSitterModeFromLanguage("markdown-inline-injection", *language, querySource);
        return mode.highlight;
    }

    // Shared by CollectInjectedHighlightSpans and CollectInjectionRegions: one
    // raw (uncanonicalized) injection-language tag paired with its
    // injection.content capture's byte range, per matched pattern instance
    // that has both. A match missing either contributes nothing, same "host's
    // own span/no region for that range" convention both public functions
    // document.
    struct RawInjectionMatch {
        std::string                   languageTag; // as written in the query/#set!, not yet canonicalized
        treesitter::QueryMatchCapture content;
    };

    std::vector<RawInjectionMatch> CollectRawInjectionMatches(const treesitter::Node& root, std::string_view bufferText,
                                                              const treesitter::Query& injectionQuery) {
        std::vector<RawInjectionMatch> matches;
        for (const treesitter::QueryMatch& match : injectionQuery.Matches(root, bufferText)) {
            std::optional<std::string_view>              language;
            std::optional<treesitter::QueryMatchCapture> content;
            for (const treesitter::QueryMatchCapture& capture : match.captures) {
                if (!language && capture.name == "injection.language") {
                    language = bufferText.substr(capture.startByte, capture.endByte - capture.startByte);
                }
                else if (!content && capture.name == "injection.content") {
                    content = capture;
                }
            }
            if (!language) {
                if (const auto it = match.setDirectives.find("injection.language"); it != match.setDirectives.end()) {
                    language = std::string_view(it->second);
                }
            }
            if (!language || !content) {
                continue;
            }
            matches.push_back(RawInjectionMatch{.languageTag = std::string(*language), .content = *content});
        }
        return matches;
    }

} // namespace

const HighlightFunction* ResolveEmbeddedLanguageHighlight(std::string_view tag, EmbeddedLanguageCache& cache) {
    const std::string canonical = CanonicalEmbeddedLanguageName(tag);
    auto              it        = cache.find(canonical);
    if (it == cache.end()) {
        std::optional<HighlightFunction> resolved;
        if (const std::optional<Mode> subMode = ModeByName(canonical + "-mode"); subMode && subMode->highlight) {
            resolved = subMode->highlight;
        }
        else {
            resolved = BuildGrammarOnlyHighlight(canonical);
        }
        it = cache.emplace(canonical, std::move(resolved)).first;
    }
    return it->second ? &*it->second : nullptr;
}

void CollectInjectedHighlightSpans(const treesitter::Node& root, std::string_view bufferText,
                                   const treesitter::Query& injectionQuery, EmbeddedLanguageCache& cache,
                                   std::vector<HighlightSpan>& spans) {
    for (const RawInjectionMatch& match : CollectRawInjectionMatches(root, bufferText, injectionQuery)) {
        const HighlightFunction* highlight = ResolveEmbeddedLanguageHighlight(match.languageTag, cache);
        if (!highlight) {
            continue;
        }
        const std::size_t      start    = match.content.startByte;
        const std::string_view codeText = bufferText.substr(start, match.content.endByte - start);
        for (const HighlightSpan& span : (*highlight)(codeText)) {
            spans.push_back(HighlightSpan{.startByte   = start + span.startByte,
                                          .endByte     = start + span.endByte,
                                          .syntaxClass = span.syntaxClass,
                                          .captureId   = span.captureId});
        }
    }
}

std::vector<InjectionRegion> CollectInjectionRegions(const treesitter::Node& root, std::string_view bufferText,
                                                     const treesitter::Query& injectionQuery) {
    std::vector<InjectionRegion> regions;
    for (const RawInjectionMatch& match : CollectRawInjectionMatches(root, bufferText, injectionQuery)) {
        regions.push_back(InjectionRegion{.startByte = match.content.startByte,
                                          .endByte   = match.content.endByte,
                                          .language  = CanonicalEmbeddedLanguageName(match.languageTag)});
    }
    return regions;
}

} // namespace ned::editor
