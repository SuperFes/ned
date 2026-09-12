//
// Mode.cpp's building blocks, shared with the per-language escapes under
// Languages/ (an escape installs a highlight or indent closure that must
// resolve captures and collect spans exactly the way the generic build
// does). Internal to the mode-construction layer: nothing outside
// Editor/Mode.cpp, Editor/LanguageDefinition.cpp and Editor/Languages/
// should include this.
//

#ifndef NED_EDITOR_MODEINTERNAL_H
#define NED_EDITOR_MODEINTERNAL_H

#include <cstddef>
#include <functional>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

#include "Mode.h"

namespace ned::editor {

// Capture name -> SyntaxClass: user remap, then the language's own defaults
// (LanguageDefinition::captureClasses), then the shared table, at every
// dotted level from most to least specific. `language` is the language key
// ("cpp"); empty for a caller that has none.
[[nodiscard]] SyntaxClass SyntaxClassForCapture(std::string_view captureName, std::string_view language = {});

// False for the capture names a query uses for its own bookkeeping rather
// than to colour anything: "_"-prefixed predicate helpers, "spell"/
// "nospell", "none".
[[nodiscard]] bool IsHighlightableCapture(std::string_view captureName);

// Cyclic heading-level class from a heading's depth (1-based).
[[nodiscard]] SyntaxClass HeadlineLevelForStarCount(std::size_t starCount);

// Collects query captures into HighlightSpans, letting the more specific
// capture name win when two patterns capture the exact same range -- see
// the definition's own comment for the json case that made this necessary.
class SpanCollector {
  public:
    void                                     Add(std::string_view captureName, std::size_t startByte, std::size_t endByte, SyntaxClass syntaxClass);
    [[nodiscard]] std::vector<HighlightSpan> Take();

  private:
    std::vector<HighlightSpan>                                 spans_;
    std::vector<int>                                           specificity_;
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> byRange_;
};

// A Mode::lineInspect closure over its own parser (runs only on an explicit
// dap-line-inspect, never per repaint); `matches` says which named node
// types count as candidate sub-expressions.
[[nodiscard]] LineInspectFunction BuildLineInspectFunction(const treesitter::Language&           language,
                                                           std::function<bool(std::string_view)> matches);

} // namespace ned::editor

#endif // NED_EDITOR_MODEINTERNAL_H
