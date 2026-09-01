#include "ListPopup.h"

#include <algorithm>
#include <utility>

#include "Border.h"
#include "KeyTranslation.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    bool IsPlainCharacter(const editor::KeyChord& chord) {
        return !chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint != 0;
    }

    bool IsQuit(const editor::KeyChord& chord) {
        return chord.Special == editor::SpecialKey::Escape || (chord.Control && chord.Codepoint == U'g');
    }

    // UTF-8-row-text follow-up: writes text one codepoint per cell (not one
    // byte per cell) -- a row's left/main text was originally always plain
    // ASCII (key chords, command names), so byte-per-cell worked by
    // accident; a multi-byte glyph (e.g. an arrow marking a candidate list's
    // scrolled-off boundary) split across cells as garbage bytes otherwise,
    // confirmed live. Returns the column x ended at.
    int PaintRowText(Canvas& c, int x, int width, int row, const std::string& text, const Brush& brush) {
        std::size_t pos = 0;
        while (pos < text.size() && x < width - 1) {
            const std::size_t next = text::NextCodepointBoundary(text, pos);
            Cell&              cell = c[{.x = x, .y = row}];
            cell.character          = text.substr(pos, next - pos);
            brush.ApplyTo(cell);
            ++x;
            pos = next;
        }
        return x;
    }

    // One column per codepoint, the same crude-but-consistent approximation
    // PaintRowText's own per-codepoint cell writes already make (no
    // grapheme-cluster/east-asian-width accounting anywhere in this
    // widget) -- used to reserve the right column's own width before
    // painting it.
    int DisplayColumnCount(const std::string& text) {
        int         count = 0;
        std::size_t pos   = 0;
        while (pos < text.size()) {
            pos = text::NextCodepointBoundary(text, pos);
            ++count;
        }
        return count;
    }

} // namespace

ListPopup::ListPopup(const Theme& theme) : theme_(theme) {
}

void ListPopup::SetModel(ListPopupModel model) {
    model_ = std::move(model);
}

int ListPopup::ContentRowCount() const {
    return static_cast<int>(model_.rows.size()) + 2; // + top/bottom border rows
}

std::optional<Point> ListPopup::Anchor() const {
    return model_.anchor;
}

void ListPopup::SetFocusable(bool focusable) {
    focusable_ = focusable;
}

void ListPopup::SetOnHighlightChange(std::function<void(std::size_t)> onHighlightChange) {
    onHighlightChange_ = std::move(onHighlightChange);
}

void ListPopup::SetOnActivate(std::function<void(std::size_t)> onActivate) {
    onActivate_ = std::move(onActivate);
}

void ListPopup::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void ListPopup::SetOnKey(std::function<void(const editor::KeyChord&)> onKey) {
    onKey_ = std::move(onKey);
}

void ListPopup::Paint(Canvas c) {
    const int width  = c.size().width;
    const int height = c.size().height;
    if (width <= 0 || height <= 0) {
        return;
    }

    const Brush chordBrush{.background = theme_.background, .foreground = theme_.borderAccent.foreground, .bold = true};
    const Brush labelBrush{.background = theme_.background, .foreground = theme_.defaultForeground};
    const Brush selectedChordBrush{
        .background = theme_.selectionBackground, .foreground = theme_.borderAccent.foreground, .bold = true};
    const Brush selectedLabelBrush{.background = theme_.selectionBackground, .foreground = theme_.defaultForeground};

    // Fill the interior with the popup's own background before drawing
    // anything else -- otherwise only the specific cells a row's text lands
    // on ever get written, and every other cell (gaps, short labels, empty
    // rows below the last one) keeps showing whatever the pane underneath
    // painted on a prior frame instead of a solid box (confirmed live
    // against the original which-key popup: buffer text bleeding through
    // its own "empty" cells).
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            Cell& cell     = c[{.x = x, .y = y}];
            cell.character = " ";
            labelBrush.ApplyTo(cell);
        }
    }

    DrawBorder(c, theme_.border);
    DrawBorderTitle(c, model_.title, theme_.borderAccent);

    int row = 1;
    for (std::size_t i = 0; i < model_.rows.size(); ++i) {
        if (row >= height - 1) {
            break; // more rows than fit -- truncated, same convention as EchoArea's own message overflow
        }
        const bool  selected  = model_.selectedIndex && *model_.selectedIndex == i;
        const Brush& left     = selected ? selectedChordBrush : chordBrush;
        const Brush& mainText = selected ? selectedLabelBrush : labelBrush;

        if (selected) {
            for (int x = 1; x < width - 1; ++x) {
                c[{.x = x, .y = row}].background_color = theme_.selectionBackground;
            }
        }

        const ListPopupRow& popupRow = model_.rows[i];
        // completion-popup follow-up: leftForeground overrides the
        // accented/plain choice entirely -- see its own doc comment.
        const Brush leftOverrideBrush{
            .background = selected ? theme_.selectionBackground : theme_.background,
            .foreground = popupRow.leftForeground.value_or(Color::Default)};
        const Brush& leftBrush = popupRow.leftForeground ? leftOverrideBrush : (popupRow.accented ? left : mainText);
        int          x         = PaintRowText(c, 2, width, row, popupRow.left, leftBrush);
        x += 2; // gap between the left column and the main label

        // completion-popup follow-up: reserve the right-aligned detail
        // column (plus one gap column ahead of it) before painting `main`,
        // so a long label truncates instead of running underneath it.
        // Skipped entirely (main gets the full remaining width, as before
        // this field existed) when `right` is empty or the popup is too
        // narrow to fit it sensibly next to `main`'s own start column.
        const int rightWidth = DisplayColumnCount(popupRow.right);
        const int rightStart = width - 1 - rightWidth;
        const int mainWidth  = (rightWidth > 0 && rightStart - 1 > x) ? rightStart : width;

        PaintRowText(c, x, mainWidth, row, popupRow.main, mainText);
        if (rightWidth > 0 && rightStart - 1 > x) {
            PaintRowText(c, rightStart, width, row, popupRow.right, mainText);
        }
        ++row;
    }
}

bool ListPopup::OnEvent(const Event& event) {
    if (!focusable_ || !Focused()) {
        return false;
    }
    if (event.is_mouse()) {
        return false; // click-to-select is a future follow-up, not required by any current consumer
    }
    return HandleKeyEvent(event);
}

bool ListPopup::HandleKeyEvent(const Event& event) {
    const auto chord = TranslateKey(event);
    if (!chord) {
        return true; // focused: swallow undecodable input rather than leaking it
    }

    if (model_.rows.empty()) {
        if (IsQuit(*chord) && onCancel_) {
            onCancel_();
        }
        return true;
    }

    std::size_t selected = model_.selectedIndex.value_or(0);
    selected              = std::min(selected, model_.rows.size() - 1);

    const bool up   = chord->Special == editor::SpecialKey::Up || (chord->Control && chord->Codepoint == U'p');
    const bool down = chord->Special == editor::SpecialKey::Down || (chord->Control && chord->Codepoint == U'n');
    if (up || down) {
        const std::size_t count = model_.rows.size();
        selected                = down ? (selected + 1) % count : (selected + count - 1) % count;
        model_.selectedIndex    = selected;
        if (onHighlightChange_) {
            onHighlightChange_(selected);
        }
        return true;
    }

    // Digit keys are a single-keystroke equivalent to arrowing to that row
    // and pressing Enter -- generalizes the one-keypress convenience LSP
    // code action select already had before this widget existed, so every
    // focus-mode consumer gets it for free.
    if (IsPlainCharacter(*chord) && chord->Codepoint >= U'1' && chord->Codepoint <= U'9') {
        const std::size_t index = static_cast<std::size_t>(chord->Codepoint - U'1');
        if (index < model_.rows.size()) {
            model_.selectedIndex = index;
            if (onActivate_) {
                onActivate_(index);
            }
        }
        return true;
    }

    if (chord->Special == editor::SpecialKey::Enter) {
        if (onActivate_) {
            onActivate_(selected);
        }
        return true;
    }

    if (IsQuit(*chord)) {
        if (onCancel_) {
            onCancel_();
        }
        return true;
    }

    if (onKey_) {
        onKey_(*chord);
    }
    return true; // every other key is consumed while this widget holds focus
}

} // namespace ned::ui
