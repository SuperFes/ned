#include "Format.h"

#include "MaxConsecutiveBlankLines.h"
#include "Text/WhitespaceHygiene.h"
#include "TrimOnSave.h"
#include "FinalNewline.h"

namespace ned::editor {

bool ApplyHygienePass(text::Buffer& buffer) {
    const std::string original = buffer.Text();
    std::string       result   = original;

    if (TrimTrailingWhitespaceOnSave()) {
        result = text::TrimTrailingWhitespaceAndBlankLines(std::move(result));
    }
    if (const std::optional<int> maxBlank = MaxConsecutiveBlankLines()) {
        result = text::CollapseBlankLineRuns(std::move(result), *maxBlank);
    }
    if (EnsureFinalNewline()) {
        result = text::EnsureTrailingNewline(std::move(result));
    }

    if (result == original) {
        return false;
    }

    buffer.BeginUndoGroup();
    buffer.DeleteRange(0, buffer.Size());
    buffer.InsertAt(0, result);
    buffer.EndUndoGroup();
    return true;
}

} // namespace ned::editor
