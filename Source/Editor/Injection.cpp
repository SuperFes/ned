#include "Injection.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

#include "BundledLanguages.h"
#include "LanguageDefinition.h"
#include "LanguageRegistry.h"
#include "ModeOverrides.h"
#include "TreeSitter/Languages.h"

namespace ned::editor {

namespace {

    // Common shorthand tags people actually type (a Markdown fence tag, an
    // HTML/injections.scm #set! value, ...), mapped to Ned's canonical
    // treesitter::LanguageByName/ModeByName spelling -- each language's own
    // definition declares the tags that mean it (language.janet's
    // :injection-aliases: "js" on javascript, "yml" on yaml,
    // "markdown_inline" on markdown-inline). A tag no definition claims
    // passes through unchanged (lowercased), so exact canonical names
    // (python, html, ...) work with no entry anywhere.
    std::string CanonicalEmbeddedLanguageName(std::string_view tag) {
        static const std::unordered_map<std::string, std::string> kAliases = [] {
            std::unordered_map<std::string, std::string> built;
            for (const LanguageDefinition& definition : BundledLanguages()) {
                for (const std::string& alias : definition.injectionAliases) {
                    built.emplace(alias, definition.name);
                }
            }
            return built;
        }();
        std::string lower(tag);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // Registered languages first (their alias sets can change at
        // runtime, so they can't fold into the static map above), then the
        // bundled map.
        for (const RegisteredLanguage& registered : RegisteredLanguages()) {
            for (const std::string& alias : registered.definition.injectionAliases) {
                if (alias == lower) {
                    return registered.definition.name;
                }
            }
        }
        const auto it = kAliases.find(lower);
        return it != kAliases.end() ? it->second : lower;
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
        // A highlighting-only grammar with no file type of its own
        // (markdown-inline) is a bundled definition like any other, just one
        // claiming no extensions -- so this one lookup covers it too.
        std::optional<HighlightFunction> resolved;
        if (const std::optional<Mode> subMode = ModeByName(canonical + "-mode"); subMode && subMode->highlight) {
            resolved = subMode->highlight;
        }
        it = cache.emplace(canonical, std::move(resolved)).first;
    }
    return it->second ? &*it->second : nullptr;
}

void CollectInjectedHighlightSpans(const treesitter::Node& root, std::string_view bufferText,
                                   const treesitter::Query& injectionQuery, EmbeddedLanguageCache& cache,
                                   std::vector<HighlightSpan>& spans, HighlightWindow window) {
    for (const RawInjectionMatch& match : CollectRawInjectionMatches(root, bufferText, injectionQuery)) {
        // Every injected region is its own parse, so skipping the ones with
        // no bytes in the window is where most of the saving is -- markdown
        // injects markdown_inline into *every* inline node, which on a
        // 125 KiB document is thousands of separate parses per keystroke for
        // a screenful of text. Intersection, not containment: a fenced block
        // straddling the top of the window still has to be highlighted.
        if (match.content.endByte <= window.startByte || match.content.startByte >= window.endByte) {
            continue;
        }
        const HighlightFunction* highlight = ResolveEmbeddedLanguageHighlight(match.languageTag, cache);
        if (!highlight) {
            continue;
        }
        const std::size_t      start    = match.content.startByte;
        const std::string_view codeText = bufferText.substr(start, match.content.endByte - start);
        // The inner call gets the whole region: it is already only as big as
        // the injection, and its own offsets are region-relative.
        for (const HighlightSpan& span : (*highlight)(codeText, HighlightWindow{})) {
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
