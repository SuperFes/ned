//
// Incremental-tree-sitter-reparse follow-up.
//
// Every Mode.h capability (highlight/fold/expandSelection/sexpMotion/
// importTarget) is a pure function of "the buffer's full current text",
// with no Buffer reference and no edit-delta parameter -- that's what keeps
// Mode a plain, freely-copyable value type usable from tests with a bare
// string, not just a real Buffer. That shape has no way to hand Parser::
// Parse's incremental overload the InputEdit it needs. IncrementalParseCache
// closes that gap without changing any of those signatures: it remembers the
// text from its own last call and, when the new text differs, reconstructs a
// single edit region via common-prefix/common-suffix diffing (cheap relative
// to a real parse) instead of requiring the caller to track one.
//
// This is a correct, if not always maximally minimal, description of
// whatever actually changed -- a single contiguous insert/delete/replace
// (the overwhelmingly common per-keystroke case) reconstructs exactly, while
// a multi-cursor edit or a programmatic whole-buffer replace just widens the
// invalidated region to its own outermost changed span rather than
// describing each piece separately. Either way tree-sitter still reuses
// every subtree outside that span instead of rebuilding the whole tree.
//
// per-subtree-fact-memoization follow-up: LastEdit() exposes the same
// region (as Text/OffsetRemap.h's ChangedSpan, the same vocabulary
// MatchCache reconciles against) so a capability closure that wants
// incremental reuse of its OWN derived facts doesn't have to independently
// re-diff text this cache already diffed -- one diff per generation
// transition, shared, not one per consumer. nullopt on a cache hit (the
// fast-path "unchanged" return) or the very first call (nothing to diff
// against yet), matching Update()'s own two early-return branches exactly.
//

#ifndef NED_EDITOR_GRAMMAR_INCREMENTALPARSE_H
#define NED_EDITOR_GRAMMAR_INCREMENTALPARSE_H

#include <optional>
#include <string>
#include <string_view>

#include "Parser.h"
#include "Text/OffsetRemap.h"
#include "Tree.h"

namespace ned::editor::grammar {

class IncrementalParseCache {
  public:
    // Returns the up-to-date tree for bufferText: the cached tree unchanged
    // if bufferText matches the previous call, an incremental reparse
    // against it if not, or a full parse on the very first call. The
    // returned reference is invalidated by the next call to Update.
    [[nodiscard]] const Tree& Update(const Parser& parser, std::string_view bufferText);

    // The single changed region this Update() computed relative to the text
    // it saw on the PREVIOUS call -- nullopt if that call was a cache hit
    // (bufferText unchanged) or the first call ever (nothing to diff
    // against). Valid only immediately after Update(); a later Update()
    // call overwrites it, same lifetime discipline as the returned Tree&.
    [[nodiscard]] std::optional<text::ChangedSpan> LastEdit() const {
        return lastEdit_;
    }

  private:
    std::string                      lastText_;
    std::optional<Tree>              lastTree_;
    std::optional<text::ChangedSpan> lastEdit_;
};

} // namespace ned::editor::grammar

#endif // NED_EDITOR_GRAMMAR_INCREMENTALPARSE_H
