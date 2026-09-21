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

std::span<const FormatPass> NativeFormatPasses() {
    static constexpr std::array kPasses{
        FormatPass{"rewrite", &ComputeRewriteEdits},
        FormatPass{"arrange", &ComputeArrangeEdits},
        FormatPass{"blank", &ComputeBlankLineEdits},
        FormatPass{"wrap", &ComputeWrapEdits},
        FormatPass{"brace-placement", &ComputeBracePlacementEdits},
        FormatPass{"keyword-break", &ComputeBreakEdits},
        FormatPass{"space", &ComputeSpaceEdits},
        FormatPass{"align", &ComputeAlignEdits},
    };
    return kPasses;
}

} // namespace ned::editor
