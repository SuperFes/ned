//
// Emacs isearch, simplified: unlike real Emacs, a query change always
// re-searches fresh from the session's start point rather than trying to
// extend the current match in place first -- simpler, and produces the same
// result for the common case (typing a longer, more specific query), just
// potentially rescanning more than strictly necessary.
//
// Buffer text is materialized once at construction: isearch never mutates
// the buffer, so that snapshot can't go stale during a session.
//

#ifndef NED_EDITOR_INCREMENTALSEARCH_H
#define NED_EDITOR_INCREMENTALSEARCH_H

#include <cstddef>
#include <string>

#include "Text/Buffer.h"

namespace ned::editor {

class IncrementalSearch {
  public:
    enum class Direction { Forward, Backward };

    IncrementalSearch(text::Buffer& buffer, Direction direction);

    void AppendChar(char32_t codepoint);
    void DeleteChar(); // removes the last character of the query, if any

    // Finds the next occurrence in the same direction, continuing past the
    // current match. A no-op if the query is empty.
    void RepeatSearch();

    void Accept(); // keep the current point, end the session
    void Cancel(); // restore the original point, end the session

    [[nodiscard]] const std::string& Query() const;
    [[nodiscard]] bool                Found() const;
    // "I-search: query" / "Failing I-search: query" (backward prepends "Backward").
    [[nodiscard]] std::string         StatusText() const;

  private:
    void Search(std::size_t from);

    text::Buffer& buffer_;
    Direction     direction_;
    std::string   content_; // buffer text, materialized once
    std::string   query_;
    std::size_t   originalPoint_;
    bool          found_ = true;
};

} // namespace ned::editor

#endif // NED_EDITOR_INCREMENTALSEARCH_H
