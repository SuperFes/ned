#include "Format.h"

#include "FinalNewline.h"
#include "FormatEdit.h"
#include "FormatPasses.h"
#include "Indent.h"
#include "MaxConsecutiveBlankLines.h"
#include "Mode.h"
#include "Text/WhitespaceHygiene.h"
#include "TrimOnSave.h"

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

namespace {

    // Every pass reads a fresh capture list: an earlier pass may have shifted
    // every byte offset after its own edits.
    template <typename ComputeFn>
    bool RunCapturePass(text::Buffer& buffer, const Mode& mode, const std::string& languageKey, ComputeFn compute) {
        const std::string                 text  = buffer.Text();
        const std::vector<FormatTextEdit> edits = compute(text, languageKey, mode.formatCaptures(text));
        if (edits.empty()) {
            return false;
        }
        ApplyFormatTextEdits(buffer, edits);
        return true;
    }

} // namespace

bool ApplyNativeFormat(text::Buffer& buffer, const Mode* mode) {
    buffer.BeginUndoGroup();
    bool changed = false;
    if (mode != nullptr && mode->indentColumn) {
        changed = IndentBuffer(buffer, *mode) > 0;
    }
    if (mode != nullptr && mode->formatCaptures) {
        const std::string languageKey = LanguageKeyForMode(*mode);
        for (const FormatPass& pass : NativeFormatPasses()) {
            changed = RunCapturePass(buffer, *mode, languageKey, pass.compute) || changed;
        }
    }
    changed = ApplyHygienePass(buffer) || changed;
    buffer.EndUndoGroup();
    return changed;
}

} // namespace ned::editor
