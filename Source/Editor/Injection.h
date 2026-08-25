//
// Generic tree-sitter "language injection" engine (embedded-language-
// injection follow-up): given a host grammar's own real injections.scm
// query -- the same declarative file every real tree-sitter tool reads,
// e.g. HTML's `(script_element (raw_text) @injection.content) (#set!
// injection.language "javascript")` -- resolves each matched region's
// target language and re-highlights it with that language's own
// HighlightFunction, offset-translating the result back into the host
// buffer's coordinates. Replaces hand-writing a bespoke C++ tree-walk per
// host grammar (Markdown fenced code blocks and inline formatting were both
// one-off passes before this existed); a new consumer just needs its own
// injections.scm embedded and one call into CollectInjectedHighlightSpans.
//

#ifndef NED_EDITOR_INJECTION_H
#define NED_EDITOR_INJECTION_H

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Mode.h"
#include "TreeSitter/Node.h"
#include "TreeSitter/Query.h"

namespace ned::editor {

// Caches a resolved language name's HighlightFunction across repeated
// CollectInjectedHighlightSpans calls -- one instance per injection-capable
// Mode's highlight closure, mirroring MarkdownMode()'s own shared_ptr-
// captured-by-value IncrementalParseCache idiom.
using EmbeddedLanguageCache = std::unordered_map<std::string, std::optional<HighlightFunction>>;

// Resolves a raw injection-language string (a fence tag, a #set! value, a
// captured tag identifier's text) to another mode's own HighlightFunction,
// reusing that language's already-correct grammar/query/capture-class
// mapping wholesale rather than a second {language -> query} table.
// Two-tier: (1) a real bundled buffer Mode via ModeByName(name + "-mode");
// (2) for a highlighting-only sub-grammar with no real Mode of its own
// (currently just "markdown-inline" -- see TreeSitter/Languages.cpp's own
// comment on why it has no ModeByName entry), a small fixed table building a
// raw TreeSitterModeFromLanguage(...) highlight instead of ModeByName's
// lookup. Caches std::nullopt on an unresolvable tag too, so repeated misses
// don't retry resolution every call.
[[nodiscard]] const HighlightFunction* ResolveEmbeddedLanguageHighlight(std::string_view       tag,
                                                                        EmbeddedLanguageCache& cache);

// Walks every match of injectionQuery against root, resolves each match's
// injection language -- a captured "injection.language" node's own text if
// present, else the match's "injection.language" #set! string operand --
// via ResolveEmbeddedLanguageHighlight, and appends offset-translated spans
// from that language's own highlight of the "injection.content" capture's
// byte range into spans. A match with no resolvable language, no
// injection.content capture, or an unresolvable language tag contributes
// nothing -- the host's own existing span for that range is left as-is. A
// match with several same-named injection.language captures uses the first.
void CollectInjectedHighlightSpans(const treesitter::Node& root, std::string_view bufferText,
                                   const treesitter::Query& injectionQuery, EmbeddedLanguageCache& cache,
                                   std::vector<HighlightSpan>& spans);

// embedded-language-documents follow-up: the same match-walk/resolution
// CollectInjectedHighlightSpans does, but returning the raw (host-buffer
// byte range, canonical language) pairs instead of running each region
// through a HighlightFunction -- what Editor/EmbeddedDocuments.h's
// BuildEmbeddedDocuments needs to sync an embedded region to its own real LSP
// server, as opposed to just coloring it. A match with no resolvable
// language or no injection.content capture contributes nothing, same as
// CollectInjectedHighlightSpans -- unlike that function, there's no
// HighlightFunction resolution step here at all (ResolveEmbeddedLanguageHighlight/
// EmbeddedLanguageCache is a highlighting-only concept, not a language-identity
// one), so this never fails to report a region just because no bundled Mode
// exists for its language.
[[nodiscard]] std::vector<InjectionRegion> CollectInjectionRegions(const treesitter::Node& root, std::string_view bufferText,
                                                                   const treesitter::Query& injectionQuery);

} // namespace ned::editor

#endif // NED_EDITOR_INJECTION_H
