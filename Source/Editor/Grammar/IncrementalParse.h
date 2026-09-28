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
// EditSince() exposes the same region, together with where the reparse
// changed the tree's structure (TreeEdit.h -- the vocabulary MatchCache
// reconciles against), so a capability closure that wants incremental reuse
// of its OWN derived facts doesn't have to independently re-diff text or
// trees this cache already diffed -- one diff per generation transition,
// shared, not one per consumer.
//

#ifndef NED_EDITOR_GRAMMAR_INCREMENTALPARSE_H
#define NED_EDITOR_GRAMMAR_INCREMENTALPARSE_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "Parser.h"
#include "Tree.h"
#include "TreeEdit.h"

namespace ned::editor::grammar {

class IncrementalParseCache {
  public:
    // Returns the up-to-date tree for bufferText: the cached tree unchanged
    // if bufferText matches the previous call, an incremental reparse
    // against it if not, or a full parse on the very first call. The
    // returned reference is invalidated by the next call to Update.
    [[nodiscard]] const Tree& Update(const Parser& parser, std::string_view bufferText);

    // Bumped whenever Update() sees text different from the previous call
    // (0 before the first), so a consumer can memoize facts derived from
    // the tree without keeping its own copy of the text to compare.
    [[nodiscard]] std::uint64_t Generation() const {
        return generation_;
    }

    // The edit that turned generation `since`'s text into the current one,
    // with the structural changes it caused -- nullopt unless `since` is
    // exactly the previous generation. Keyed on the consumer's own last
    // generation rather than on the last Update() call, so it stays right
    // whichever of several capabilities sharing this cache saw the new text
    // first.
    [[nodiscard]] std::optional<TreeEdit> EditSince(std::uint64_t since) const {
        return since + 1 == generation_ ? generationEdit_ : std::nullopt;
    }

  private:
    std::string                      lastText_;
    std::optional<Tree>              lastTree_;
    std::uint64_t                    generation_ = 0;
    std::optional<TreeEdit>          generationEdit_;
};

} // namespace ned::editor::grammar

#endif // NED_EDITOR_GRAMMAR_INCREMENTALPARSE_H
