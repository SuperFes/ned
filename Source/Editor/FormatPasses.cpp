#include "FormatPasses.h"

#include <array>

#include "FormatAlign.h"
#include "FormatArrange.h"
#include "FormatBlankLines.h"
#include "FormatBracePlacement.h"
#include "FormatBreak.h"
#include "FormatRewrite.h"
#include "FormatSpacing.h"
#include "FormatWrap.h"

namespace ned::editor {

namespace {

    // A pass whose output doesn't depend on the indent style.
    template <auto Compute>
    std::vector<FormatTextEdit> IgnoringStyle(std::string_view text, std::string_view languageKey,
                                              const std::vector<FormatCapture>& captures, const IndentStyle&) {
        return Compute(text, languageKey, captures);
    }

} // namespace

std::span<const FormatPass> NativeFormatPasses() {
    static constexpr std::array kPasses{
        FormatPass{"rewrite", &IgnoringStyle<&ComputeRewriteEdits>},
        FormatPass{"arrange", &IgnoringStyle<&ComputeArrangeEdits>},
        FormatPass{"blank", &IgnoringStyle<&ComputeBlankLineEdits>},
        FormatPass{"wrap", &ComputeWrapEdits},
        FormatPass{"brace-placement", &ComputeBracePlacementEdits},
        FormatPass{"keyword-break", &IgnoringStyle<&ComputeBreakEdits>},
        FormatPass{"space", &IgnoringStyle<&ComputeSpaceEdits>},
        FormatPass{"align", &IgnoringStyle<&ComputeAlignEdits>},
    };
    return kPasses;
}

} // namespace ned::editor
