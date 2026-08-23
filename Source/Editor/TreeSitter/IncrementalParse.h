//
// Incremental-tree-sitter-reparse follow-up.
//
// Every Mode.h capability (highlight/fold/expandSelection/sexpMotion/
// importTarget) is a pure function of "the buffer's full current text",
// with no Buffer reference and no edit-delta parameter -- that's what keeps
// Mode a plain, freely-copyable value type usable from tests with a bare
// string, not just a real Buffer. That shape has no way to hand Parser::
// Parse's incremental overload the TSInputEdit it needs. IncrementalParseCache
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

#ifndef NED_EDITOR_TREESITTER_INCREMENTALPARSE_H
#define NED_EDITOR_TREESITTER_INCREMENTALPARSE_H

#include <optional>
#include <string>
#include <string_view>

#include "Parser.h"
#include "Tree.h"

namespace ned::editor::treesitter {

class IncrementalParseCache {
  public:
    // Returns the up-to-date tree for bufferText: the cached tree unchanged
    // if bufferText matches the previous call, an incremental reparse
    // against it if not, or a full parse on the very first call. The
    // returned reference is invalidated by the next call to Update.
    [[nodiscard]] const Tree& Update(const Parser& parser, std::string_view bufferText);

  private:
    std::string         lastText_;
    std::optional<Tree> lastTree_;
};

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_INCREMENTALPARSE_H
