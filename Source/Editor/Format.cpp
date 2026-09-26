#include "Format.h"

#include "BufferSave.h"
#include "FileSettings.h"
#include "FinalNewline.h"
#include "FormatEdit.h"
#include "FormatOnSave.h"
#include "FormatPasses.h"
#include "Indent.h"
#include "MaxConsecutiveBlankLines.h"
#include "Mode.h"
#include "ModeOverrides.h"
#include "Text/WhitespaceHygiene.h"
#include "TrimOnSave.h"

namespace ned::editor {

bool ApplyHygienePass(text::Buffer& buffer, const Mode* mode) {
    const std::string original = buffer.Text();
    std::string       result   = original;

    // Trimming never adds or removes a line before a kept one, so the line
    // numbers still hold for the collapse after it.
    const std::vector<std::size_t> keep =
        mode != nullptr && mode->keptTrailingWhitespace ? mode->keptTrailingWhitespace(original) : std::vector<std::size_t>{};
    if (TrimTrailingWhitespaceOnSave(buffer)) {
        result = text::TrimTrailingWhitespaceAndBlankLines(std::move(result), keep);
    }
    if (const std::optional<int> maxBlank = MaxConsecutiveBlankLines()) {
        result = text::CollapseBlankLineRuns(std::move(result), *maxBlank, keep);
    }
    if (EnsureFinalNewline(buffer)) {
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

bool FormatEditsKeepStructure(const Mode& mode, std::string_view text, const std::vector<FormatTextEdit>& edits) {
    if (edits.empty() || !mode.sameStructure) {
        return true;
    }
    return mode.sameStructure(text, ApplyFormatTextEditsToText(text, edits));
}

namespace {

    // Every pass reads a fresh capture list: an earlier pass may have shifted
    // every byte offset after its own edits.
    template <typename ComputeFn>
    bool RunCapturePass(text::Buffer& buffer, const Mode& mode, const std::string& languageKey, ComputeFn compute) {
        const std::string                 text  = buffer.Text();
        const std::vector<FormatTextEdit> edits =
            compute(text, languageKey, mode.formatCaptures(text), EffectiveIndentStyle(buffer, mode.name));
        if (edits.empty() || !FormatEditsKeepStructure(mode, text, edits)) {
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
        bool              passChanged = false;
        for (const FormatPass& pass : NativeFormatPasses()) {
            passChanged = RunCapturePass(buffer, *mode, languageKey, pass.compute) || passChanged;
        }
        // A body's indent can depend on where its brace sat: `{` alone on the
        // line after `x <- function()` is a continuation line, and so was
        // everything under it until the brace moved up.
        if (passChanged && mode->indentColumn) {
            IndentBuffer(buffer, *mode);
        }
        changed = passChanged || changed;
    }
    changed = ApplyHygienePass(buffer, mode) || changed;
    buffer.EndUndoGroup();
    return changed;
}

void FormatFileOnDisk(const std::filesystem::path& path) {
    text::Buffer buffer = text::Buffer::FromFile(path);
    ApplyFileSettings(buffer);
    const Mode mode = ModeForPath(path);

    std::optional<std::string> formatted;
    if (FormatCommand()) {
        formatted = RunFormatCommand(buffer.Text());
    }
    if (formatted) {
        buffer.DeleteRange(0, buffer.Size());
        buffer.InsertAt(0, *formatted);
    }
    else {
        ApplyNativeFormat(buffer, &mode);
    }
    WriteBufferToDisk(buffer);
}

} // namespace ned::editor
