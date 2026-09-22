#include "InjectedIndent.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "EmbeddedDocuments.h"
#include "ModeOverrides.h"

namespace ned::editor {

namespace {

    std::mutex& EnabledMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& EnabledStorage() {
        static bool enabled = true;
        return enabled;
    }

    // How deep an injection chain this will follow. A resolved inner mode's
    // own indentColumn is injection-aware too (HTML injects JavaScript,
    // which in a template literal can inject HTML again), so the recursion
    // is real and needs a stop, not just a convention.
    constexpr unsigned kMaxInjectionDepth = 3;

    unsigned& Depth() {
        thread_local unsigned depth = 0;
        return depth;
    }

    std::size_t FirstNonBlankByte(std::string_view text, std::size_t lineStart, std::size_t lineEnd) {
        std::size_t offset = lineStart;
        while (offset < lineEnd && offset < text.size() && (text[offset] == ' ' || text[offset] == '\t')) {
            offset++;
        }
        return offset;
    }

    std::size_t LineStartFor(std::string_view text, std::size_t byteOffset) {
        const std::size_t previousNewline = text.rfind('\n', byteOffset == 0 ? 0 : byteOffset - 1);
        return previousNewline == std::string_view::npos ? 0 : previousNewline + 1;
    }

    std::size_t LineEndFor(std::string_view text, std::size_t byteOffset) {
        const std::size_t newline = text.find('\n', byteOffset);
        return newline == std::string_view::npos ? text.size() : newline;
    }

    bool ByteInRanges(std::size_t byteOffset, const std::vector<std::pair<std::size_t, std::size_t>>& ranges) {
        return std::ranges::any_of(ranges, [byteOffset](const std::pair<std::size_t, std::size_t>& range) {
            return byteOffset >= range.first && byteOffset < range.second;
        });
    }

    // Where a document starts, in both languages' terms: `host` is what the
    // host grammar indents the document's first owned line to -- the column
    // it placed the whole region at -- and `injected` is what the injected
    // language indents that same line to, the column its own structure calls
    // "the start". Every other line of the document is then host + (its own
    // injected indent - injected).
    //
    // Measured once per document, at its first line, rather than per line:
    // asking the host again for every line would double-count a host
    // construct that interleaves with the injected one, which is exactly
    // what a PHP template's `<?php foreach (...) { ?>` does -- its brace
    // opens a PHP block around HTML that is already nesting itself, and the
    // rows inside it came out at both languages' depths added together.
    struct DocumentOrigin {
        std::optional<int> host;
        std::optional<int> injected;
        bool               measured = false;
    };

    // Everything the wrapper remembers between calls. Rebuilt whenever the
    // text it was derived from changes; a whole-buffer reindent freezes its
    // text for the entire run (Indent.cpp's IndentRegion), so that is one
    // synthesis for the document rather than one per line.
    struct InjectedIndentCache {
        std::string                                                    text;
        std::vector<EmbeddedDocument>                                  documents;
        std::unordered_map<std::string, std::optional<IndentFunction>> indentByLanguage;
        std::vector<DocumentOrigin>                                    origins;
    };

    const IndentFunction* ResolveInjectedIndent(const std::string& language, InjectedIndentCache& cache) {
        auto found = cache.indentByLanguage.find(language);
        if (found == cache.indentByLanguage.end()) {
            std::optional<IndentFunction> resolved;
            if (const std::optional<Mode> mode = ModeByName(language + "-mode"); mode && mode->indentColumn) {
                resolved = mode->indentColumn;
            }
            found = cache.indentByLanguage.emplace(language, std::move(resolved)).first;
        }
        return found->second ? &*found->second : nullptr;
    }

    const DocumentOrigin& OriginFor(const EmbeddedDocument& document, const IndentFunction& injectedIndent,
                                    const IndentFunction& hostIndent, std::string_view bufferText, std::size_t index,
                                    InjectedIndentCache& cache) {
        if (index >= cache.origins.size()) {
            cache.origins.resize(index + 1);
        }
        DocumentOrigin& origin = cache.origins[index];
        if (origin.measured) {
            return origin;
        }
        origin.measured = true;
        if (document.ownedRanges.empty()) {
            return origin;
        }

        // The first line the document OWNS, not the first owned byte's line:
        // a region opening mid-line (HTML's "<script>" shares a line with the
        // JavaScript that follows it) would otherwise measure a line the
        // host, not the injected language, is responsible for.
        const std::size_t firstOwned = document.ownedRanges.front().first;
        std::size_t       lineStart  = LineStartFor(document.documentText, firstOwned);
        if (lineStart < firstOwned) {
            const std::size_t lineEnd = LineEndFor(document.documentText, firstOwned);
            if (lineEnd >= document.documentText.size()) {
                return origin;
            }
            lineStart = lineEnd + 1;
        }

        const std::size_t lineEnd = LineEndFor(document.documentText, lineStart);
        origin.injected           = injectedIndent(document.documentText, lineStart, lineEnd);
        origin.host               = hostIndent ? hostIndent(bufferText, lineStart, lineEnd) : std::optional<int>(0);
        return origin;
    }

} // namespace

void SetIndentInjectedRegions(bool enabled) {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    EnabledStorage() = enabled;
}

bool IndentInjectedRegions() {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    return EnabledStorage();
}

IndentFunction WithInjectedRegionIndent(IndentFunction host, EmbeddedRegionFunction regions) {
    if (!regions) {
        return host;
    }

    const auto cache = std::make_shared<InjectedIndentCache>();
    return [host = std::move(host), regions = std::move(regions),
            cache](std::string_view bufferText, std::size_t lineStart, std::size_t lineEnd) -> std::optional<int> {
        const std::optional<int> hostColumn = host ? host(bufferText, lineStart, lineEnd) : std::nullopt;
        if (!IndentInjectedRegions() || Depth() >= kMaxInjectionDepth) {
            return hostColumn;
        }

        if (cache->text != bufferText) {
            cache->text = std::string(bufferText);
            cache->origins.clear();
            Depth()++;
            cache->documents = BuildInjectedDocuments(regions(bufferText), bufferText);
            Depth()--;
        }
        if (cache->documents.empty()) {
            return hostColumn;
        }

        // What this line is written in is decided by its first real
        // character, not by where the line happens to start: the line
        // holding "<?= date(...) ?>" in a PHP template is HTML that has a
        // PHP island in it, and it indents as the HTML it is.
        const std::size_t owner = FirstNonBlankByte(bufferText, lineStart, lineEnd);
        for (std::size_t index = 0; index < cache->documents.size(); index++) {
            const EmbeddedDocument& document = cache->documents[index];
            if (!ByteInRanges(owner, document.ownedRanges)) {
                continue;
            }

            const IndentFunction* indent = ResolveInjectedIndent(document.language, *cache);
            if (indent == nullptr) {
                break;
            }

            Depth()++;
            const std::optional<int> injected = (*indent)(document.documentText, lineStart, lineEnd);
            const DocumentOrigin&    origin   = OriginFor(document, *indent, host, bufferText, index, *cache);
            Depth()--;

            if (!injected || !origin.injected || !origin.host) {
                break;
            }
            return std::max(0, *origin.host + *injected - *origin.injected);
        }

        return hostColumn;
    };
}

} // namespace ned::editor
