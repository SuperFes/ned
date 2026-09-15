//
// wrap-kind follow-up (kind 4 of Docs/FormattingCapabilities.md's
// nine-rule-kind catalogue): the pilot Wrap pass, over cpp's own
// "wrap.args" pilot capture (a call's argument list,
// Source/Languages/cpp/format.janet) -- the same "one pilot construct,
// full chain end to end" discipline Break's own pilot (FormatBracePlacement.h)
// established. Only the two margin-independent policy values are
// implemented (WrapRuleValue's own header comment explains why
// wrap-if-long/chop-down-if-long are a deliberately separate follow-up).
//
// Pure/buffer-free compute, thin Buffer-mutating apply -- the same split
// every other rule-kind pass in this codebase already makes.
//

#ifndef NED_EDITOR_FORMATWRAP_H
#define NED_EDITOR_FORMATWRAP_H

#include <string_view>
#include <vector>

#include "FormatEdit.h"
#include "Mode.h"

namespace ned::editor {

// Computes the edits needed to make every capture in `captures` whose name
// resolves a WrapRuleFor(name, languageKey).policy match it. A capture with
// no configured policy, or with no ".item" captures of its own
// (FormatCapture::items, Mode.cpp's own correlation), contributes no edit at
// all -- an unconfigured capture is a total no-op, the same "nothing forced
// by default" rule every other rule kind here already follows.
//
// Only the interior between a capture's own open delimiter and close
// delimiter is ever touched (FormatCapture::openLength/closeLength mark
// exactly where that interior starts/ends) -- nothing before the capture,
// nothing inside an item's own text. Idempotent by construction: re-running
// against the result of a previous run recomputes the same desired interior
// and finds it already present, emitting nothing.
[[nodiscard]] std::vector<FormatTextEdit> ComputeWrapEdits(std::string_view                  text,
                                                           std::string_view                  languageKey,
                                                           const std::vector<FormatCapture>& captures);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATWRAP_H
