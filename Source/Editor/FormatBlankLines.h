//
// blank-lines-kind pilot (kind 6, FormattingCapabilities.md's B1 "Blank
// lines"): minimum enforced / maximum preserved blank lines immediately
// before a format.janet capture's own line, reading FormatRules.h's
// BlankRuleValue exactly the way ComputeBracePlacementEdits/
// ComputeSpaceEdits read Break/Space's. See FormatRules.h's own header
// comment on BlankRuleValue for why this is "before" only and why
// minBefore/maxBefore are gated on FormatCapture::isFirst differently.
//

#ifndef NED_EDITOR_FORMATBLANKLINES_H
#define NED_EDITOR_FORMATBLANKLINES_H

#include <string_view>
#include <vector>

#include "FormatEdit.h"
#include "Mode.h"

namespace ned::editor {

[[nodiscard]] std::vector<FormatTextEdit> ComputeBlankLineEdits(std::string_view                  text,
                                                                 std::string_view                  languageKey,
                                                                 const std::vector<FormatCapture>& captures);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATBLANKLINES_H
