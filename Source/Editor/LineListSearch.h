//
// DAP round 5: debug-console-search follow-up. Emacs isearch's
// IncrementalSearch.h, adapted for searching a plain, static list of display
// lines instead of a text::Buffer -- what DebugConsolePanel's transcript
// needs (its own header comment used to record this as a deliberate v1 cut:
// "no established pattern for searching a list of lines outside a real
// Buffer"). No buffer/undo semantics apply here since the line list is
// immutable from the searcher's point of view, so this is considerably
// smaller than IncrementalSearch: matching is whole-line substring (no
// in-line match position tracked), and "point" is a line index rather than
// a byte offset.
//
// Same smart-case rule as IncrementalSearch: case-insensitive unless the
// query itself contains an uppercase letter. ASCII-only.
//
// lines is captured by reference and must outlive this object -- the same
// contract IncrementalSearch has with its buffer_.
//

#ifndef NED_EDITOR_LINELISTSEARCH_H
#define NED_EDITOR_LINELISTSEARCH_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ned::editor {

class LineListSearch {
  public:
    enum class Direction { Forward,
                           Backward };

    LineListSearch(const std::vector<std::string>& lines, Direction direction, std::size_t startIndex);

    void AppendChar(char32_t codepoint);
    void DeleteChar(); // removes the last character of the query, if any

    // Finds the next occurrence in the same direction, continuing past the
    // current match. A no-op if the query is empty.
    void RepeatSearch();

    // Flips Forward/Backward and re-searches from the current match --
    // IncrementalSearch::ReverseDirection's exact sibling.
    void ReverseDirection();

    void Accept(); // nothing to do -- the caller keeps CurrentIndex()'s position
    void Cancel(); // nothing to do -- the caller restores OriginalIndex() itself

    [[nodiscard]] const std::string& Query() const;
    [[nodiscard]] bool               Found() const;
    [[nodiscard]] Direction          CurrentDirection() const;
    // The matched line index, or nullopt when Found() is false (the query
    // stays showing wherever the last successful match left it, same as
    // IncrementalSearch's point).
    [[nodiscard]] std::optional<std::size_t> CurrentIndex() const;
    [[nodiscard]] std::size_t                OriginalIndex() const;
    // "I-search: query" / "Failing I-search: query" (backward prepends "Backward").
    [[nodiscard]] std::string StatusText() const;

  private:
    // Steps one line in direction_, wrapping.
    [[nodiscard]] std::size_t Advance(std::size_t index) const;
    // Scans starting at (and including) from, wrapping through the whole
    // list at most once.
    void Search(std::size_t from);

    const std::vector<std::string>& lines_;
    Direction                       direction_;
    std::string                     query_;
    std::size_t                     originalIndex_;
    std::optional<std::size_t>      currentIndex_;
    bool                            found_ = true;
};

} // namespace ned::editor

#endif // NED_EDITOR_LINELISTSEARCH_H
