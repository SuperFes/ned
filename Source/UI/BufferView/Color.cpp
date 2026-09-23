//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// The edit half of the colour swatches: `color-at-point` (C-c #), which
// rewrites the literal under point in another notation.
//
// The swatch itself is painted from Paint.cpp, off the same
// ColorLiteralsInRange that this file's own lookup uses -- one recogniser, so
// what earns a swatch and what the command can act on are by construction the
// same set.
//
// The list is computed, not requested: Editor/ColorLiteral.h's
// ColorPresentations() is a pure function of the colour, and every entry it
// produces is guaranteed to scan back to the colour it was offered for, so
// accepting one never shifts the value. A language server answering
// textDocument/colorPresentation contributes extra rows -- its own name for
// the colour, which is the one thing a pure conversion cannot know -- and
// that is the only reason the session opens from a callback rather than
// inline.
//

#include "UI/BufferView/Internal.h"

#include "Editor/ColorLiteral.h"
#include "Editor/ColorSwatchSettings.h"

namespace ned::ui {

using namespace detail;

namespace {

    // Opaque, unlike the swatch painted in the buffer: this one is a glyph in
    // a list, and a translucent foreground has nothing behind it to be
    // translucent against -- it would just fade the only thing in the row that
    // says what the colour is.
    Color RowSwatchColor(const editor::ColorValue& color) {
        return Color::RGB(editor::ColorChannelToByte(color.red), editor::ColorChannelToByte(color.green),
                          editor::ColorChannelToByte(color.blue));
    }

} // namespace

void BufferView::RequestColorAtPoint() {
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         point     = buffer.Point();
    const std::size_t         line      = content.ByteOffsetToLine(point);
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();

    // Deliberately the same lookup the swatches are painted from, rather than
    // a scan of its own: what carries a swatch and what this command can act
    // on have to be the same set, or the marker would be pointing at
    // something the user cannot then do anything with.
    const std::vector<editor::ColorLiteral> literals = ColorLiteralsInRange(buffer, content, lineStart, lineEnd);
    const editor::ColorLiteral* const       found    = editor::ColorLiteralContaining(literals, point);
    if (found == nullptr) {
        statusMessage_ = "No colour literal at point.";
        return;
    }

    if (inputMode_ != InputMode::Normal) {
        statusMessage_ = "Colour conversion cancelled (another prompt is open).";
        return;
    }

    const std::string current = content.Substring(found->begin, found->end - found->begin);

    // ned's own notations are ready now; a server's are worth waiting for
    // because they are the one thing a pure conversion cannot produce -- the
    // name this language has for the colour. The session therefore opens from
    // the callback, which runs immediately and synchronously when there is no
    // server to ask.
    text::Buffer* const bufferPtr = &buffer;
    const auto          open      = [this, bufferPtr, begin = found->begin, end = found->end, color = found->color,
                                     current](const std::vector<std::string>& serverPresentations) {
        if (bufferPtr != &activeBuffer_.Get() || inputMode_ != InputMode::Normal) {
            return; // the buffer or the prompt moved under the request
        }

        std::vector<editor::ColorPresentation> presentations =
            editor::ColorPresentations(color, mode_.colorLiterals);
        for (const std::string& text : serverPresentations) {
            const bool known = std::any_of(presentations.begin(), presentations.end(),
                                           [&text](const editor::ColorPresentation& presentation) {
                                               return presentation.text == text;
                                           });
            if (!known) {
                presentations.push_back(editor::ColorPresentation{.syntax = editor::ColorSyntax::Unknown,
                                                                  .text   = text});
            }
        }
        // The notation it is already written in is not an offer -- accepting
        // it would be a no-op edit that still lands in the undo tree.
        std::erase_if(presentations, [&current](const editor::ColorPresentation& presentation) {
            return presentation.text == current;
        });
        if (presentations.empty()) {
            statusMessage_ = "No other notation for " + current + ".";
            return;
        }

        pendingColorPresentations_  = std::move(presentations);
        pendingColorValue_          = color;
        pendingColorBegin_          = begin;
        pendingColorEnd_            = end;
        colorPresentationSelection_ = 0;
        inputMode_                  = InputMode::ColorPresentationSelect;
        RefreshColorPresentationStatus();
    };

    if (lspManager_ == nullptr) {
        open({});
        return;
    }
    lspManager_->RequestColorPresentations(buffer, found->color, found->begin, found->end, open,
                                           editor::LanguageKeyForMode(mode_));
}

void BufferView::RefreshColorPresentationStatus() {
    statusMessage_ = "Colour: ";
    if (!onCandidatesChanged_) {
        return;
    }
    ListPopupModel model;
    model.title = "Colour at point";
    model.rows.reserve(pendingColorPresentations_.size());
    for (const editor::ColorPresentation& presentation : pendingColorPresentations_) {
        // The swatch is the left column rather than a numbered prefix: the
        // labels are the point of the list, and "hsl(320, 100%, 50%)" is not
        // a colour anyone reads at a glance.
        model.rows.push_back(ListPopupRow{.left           = "█",
                                          .main           = presentation.text,
                                          .leftForeground = RowSwatchColor(pendingColorValue_)});
    }
    if (!pendingColorPresentations_.empty()) {
        model.selectedIndex = colorPresentationSelection_;
    }
    onCandidatesChanged_(std::move(model));
}

void BufferView::HandleColorPresentationSelectKey(const editor::KeyChord& chord) {
    HandleChoicePromptKey({.count         = pendingColorPresentations_.size(),
                           .selection     = &colorPresentationSelection_,
                           .cancelMessage = "Colour conversion cancelled.",
                           .refresh       = [this] { RefreshColorPresentationStatus(); },
                           .commit =
                               [this, presentations = pendingColorPresentations_](std::size_t index) {
                                   ApplyColorPresentation(presentations[index].text);
                               }},
                          chord);
}

void BufferView::ApplyColorPresentation(const std::string& text) {
    text::Buffer& buffer = activeBuffer_.Get();
    if (pendingColorEnd_ > buffer.Size() || pendingColorEnd_ <= pendingColorBegin_) {
        statusMessage_ = "Colour literal is no longer there.";
        return;
    }
    buffer.BeginUndoGroup();
    buffer.DeleteRange(pendingColorBegin_, pendingColorEnd_ - pendingColorBegin_);
    buffer.InsertAt(pendingColorBegin_, text);
    buffer.EndUndoGroup();
    buffer.SetPoint(pendingColorBegin_);
    statusMessage_ = "Colour rewritten as " + text + ".";
}

} // namespace ned::ui
