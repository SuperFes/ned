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

    // perf/parallel-highlighting-round-1 follow-up: window is threaded
    // through to MatchesInRange rather than the unwindowed Matches() --
    // CollectInjectedHighlightSpans's own window only used to bound which
    // FOUND regions get sub-parsed (the loop below still re-checks that),
    // leaving the injection query's own tree walk unwindowed regardless of
    // how small a window the caller actually wanted. Harmless for
    // BufferView's viewport-sized windows (cheap either way relative to the
    // sub-parses it gates), but load-bearing for Minimap's chunked sweep
    // (UI/Minimap.cpp): a small window repeated across many ticks turned an
    // O(document) "where are the injections" walk into
    // O(document * tick count), found live via a test using a
    // pathologically small chunk size that made this call alone dominate.
    // CollectInjectionRegions (the whole-document consumer, no window
    // concept of its own) gets the default unbounded window, matching its
    // existing behavior exactly.
    std::vector<RawInjectionMatch> CollectRawInjectionMatches(const treesitter::Node& root, std::string_view bufferText,
                                                              const treesitter::QueryMatcher& injectionQuery,
                                                              HighlightWindow                 window = {}) {
        std::vector<RawInjectionMatch> matches;
        for (const treesitter::QueryMatch& match : injectionQuery.MatchesInRange(root, bufferText, window.startByte,
                                                                                 std::min(window.endByte, bufferText.size()))) {
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
                                   const treesitter::QueryMatcher& injectionQuery, EmbeddedLanguageCache& cache,
                                   std::vector<HighlightSpan>& spans, HighlightWindow window) {
    for (const RawInjectionMatch& match : CollectRawInjectionMatches(root, bufferText, injectionQuery, window)) {
        // Every injected region is its own parse, so skipping the ones with
        // no bytes in the window is where most of the saving is -- markdown
        // injects markdown_inline into *every* inline node, which on a
        // 125 KiB document is thousands of separate parses per keystroke for
        // a screenful of text. Intersection, not containment: a fenced block
        // straddling the top of the window still has to be highlighted. This
        // check is now mostly a formality (CollectRawInjectionMatches above
        // already range-pruned its own tree walk to `window`), kept because
        // MatchesInRange's own guarantee is pattern-ROOT intersection, not
        // exact containment of the injection.content capture specifically.
        if (match.content.endByte <= window.startByte || match.content.startByte >= window.endByte) {
            continue;
        }
        const HighlightFunction* highlight = ResolveEmbeddedLanguageHighlight(match.languageTag, cache);
        if (!highlight) {
            continue;
        }
        const std::size_t      start    = match.content.startByte;
        const std::string_view codeText = bufferText.substr(start, match.content.endByte - start);
        // Translate the outer window into this region's own relative
        // coordinates (clamped to its bounds) rather than always asking for
        // the whole region -- an unbounded outer window (the ordinary
        // whole-document call) still translates to [0, codeText.size()),
        // identical to the old behavior, but a bounded one that only
        // partially covers a large straddling region (a big fenced code
        // block, or a chunked sweep's own small window landing partway
        // through one -- UI/Minimap.cpp's AdvanceHighlightSweep) now only
        // sub-highlights the part actually asked for. Load-bearing for the
        // sweep specifically: without this, a region straddling a chunk
        // boundary got re-emitted WHOLE by every chunk that merely
        // intersected it, breaking that caller's own "each chunk emits only
        // the spans starting within its own window" invariant -- found by
        // Tests/ChunkedHighlightTest.cpp at a deliberately small chunk size.
        const std::size_t subWindowStart = window.startByte > start ? window.startByte - start : 0;
        const std::size_t subWindowEnd   = window.endByte >= match.content.endByte
                                               ? codeText.size()
                                               : (window.endByte > start ? window.endByte - start : 0);
        for (const HighlightSpan& span :
             (*highlight)(codeText, HighlightWindow{.startByte = subWindowStart, .endByte = subWindowEnd})) {
            spans.push_back(HighlightSpan{.startByte   = start + span.startByte,
                                          .endByte     = start + span.endByte,
                                          .syntaxClass = span.syntaxClass,
                                          .captureId   = span.captureId});
        }
    }
}

std::vector<InjectionRegion> CollectInjectionRegions(const treesitter::Node& root, std::string_view bufferText,
                                                     const treesitter::QueryMatcher& injectionQuery) {
    std::vector<InjectionRegion> regions;
    for (const RawInjectionMatch& match : CollectRawInjectionMatches(root, bufferText, injectionQuery)) {
        regions.push_back(InjectionRegion{.startByte = match.content.startByte,
                                          .endByte   = match.content.endByte,
                                          .language  = CanonicalEmbeddedLanguageName(match.languageTag)});
    }
    return regions;
}

} // namespace ned::editor
