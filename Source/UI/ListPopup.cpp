#include "ListPopup.h"

#include <algorithm>
#include <cctype>
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

    // completion-popup-preview follow-up: a plain greedy word-wrap -- no
    // markdown rendering, no paragraph-break preservation (all whitespace,
    // including a literal "\n\n" a multi-entry hover-style join can
    // produce, collapses to a single space between words), matching
    // lsp-hover's own flat-text precedent. Scans raw bytes for ASCII
    // whitespace (safe against UTF-8 content: a continuation byte is
    // always >= 0x80, never mistaken for a space/tab/newline), but walks
    // a word's own span via NextCodepointBoundary so a multi-byte glyph
    // is never split. A single word wider than `width` is never
    // hyphenated -- it just overflows that one line, same "don't grow a
    // parallel narrow-case path" call DisplayColumnCount's own truncation
    // callers already make.
    std::vector<std::string> WrapText(const std::string& text, int width) {
        std::vector<std::string> lines;
        if (width <= 0) {
            return lines;
        }

        std::vector<std::string> words;
        std::size_t              pos = 0;
        while (pos < text.size()) {
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
            const std::size_t start = pos;
            while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos]))) {
                pos = text::NextCodepointBoundary(text, pos);
            }
            if (pos > start) {
                words.push_back(text.substr(start, pos - start));
            }
        }

        std::string currentLine;
        int         currentWidth = 0;
        for (const std::string& word : words) {
            const int wordWidth = DisplayColumnCount(word);
            if (!currentLine.empty() && currentWidth + 1 + wordWidth > width) {
                lines.push_back(std::move(currentLine));
                currentLine.clear();
                currentWidth = 0;
            }
            if (!currentLine.empty()) {
                currentLine += ' ';
                ++currentWidth;
            }
            currentLine += word;
            currentWidth += wordWidth;
        }
        if (!currentLine.empty()) {
            lines.push_back(std::move(currentLine));
        }
        return lines;
    }

} // namespace

ListPopup::ListPopup(const Theme& theme) : theme_(theme) {
}

void ListPopup::SetModel(ListPopupModel model) {
    model_ = std::move(model);
}

int ListPopup::ContentRowCount() const {
    int rows = static_cast<int>(model_.rows.size()) + 2; // + top/bottom border rows
    if (model_.previewText) {
        rows += 1 + kPreviewMaxLines; // + one divider row + the fixed preview budget
    }
    return rows;
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

    // completion-popup-preview follow-up: the wrapped-text footer, drawn
    // below whatever rows fit (skipped entirely once there's no room left
    // for even the divider row -- same "silently truncate" convention the
    // row loop above already follows for its own overflow).
    if (model_.previewText && row < height - 1) {
        for (int x = 1; x < width - 1; ++x) {
            c[{.x = x, .y = row}].character = text::EncodeCodepointUtf8(RoundedBorderGlyphs().horizontal);
            labelBrush.ApplyTo(c[{.x = x, .y = row}]);
        }
        ++row;

        const std::vector<std::string> lines = WrapText(*model_.previewText, width - 3); // 1-col margin each side + border
        for (std::size_t i = 0; i < lines.size() && i < static_cast<std::size_t>(kPreviewMaxLines) && row < height - 1; ++i) {
            // More wrapped lines exist past this one -- reserve the row's
            // own last column for a "…" marker rather than slicing lines[i]
            // itself (which could land mid-codepoint); PaintRowText is
            // capped one column short instead, so the marker never
            // overwrites real text.
            const bool truncated = (i + 1 == static_cast<std::size_t>(kPreviewMaxLines)) && lines.size() > static_cast<std::size_t>(kPreviewMaxLines);
            const int  lineWidth = truncated ? width - 1 : width;
            PaintRowText(c, 2, lineWidth, row, lines[i], labelBrush);
            if (truncated) {
                Cell& cell     = c[{.x = width - 2, .y = row}];
                cell.character = "…";
                labelBrush.ApplyTo(cell);
            }
            ++row;
        }
    }
}

bool ListPopup::OnEvent(const Event& event) {
    // mouse-support follow-up: mouse dispatch no longer sits behind the
    // focus guard below -- a non-focusable consumer (the completion popup)
    // never calls TakeFocus() but still wants clicks handled.
    if (event.is_mouse()) {
        return HandleMouseEvent(event);
    }
    if (!focusable_ || !Focused()) {
        return false;
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

bool ListPopup::HandleMouseEvent(const Event& event) {
    const std::optional<MouseEvent> mouse = LocalMouseEvent(event);
    if (!mouse) {
        return true; // outside this popup's own Box_() -- can't happen in practice
                     // (OverlayHost only forwards a click already inside it), but
                     // consumed regardless, matching every other mouse handler here
    }
    if (mouse->button != MouseEvent::Button::Left || mouse->motion != MouseEvent::Motion::Pressed) {
        return true; // only a left press activates a row -- everything else (release,
                     // other buttons, motion) is a no-op click-wise, still consumed
    }

    const int row = mouse->at.y - 1; // row 0 (local y=0) is the top border
    if (row < 0 || static_cast<std::size_t>(row) >= model_.rows.size()) {
        return true; // border, preview footer, or past the last row -- no target
    }

    const auto index = static_cast<std::size_t>(row);
    if (onHighlightChange_) {
        onHighlightChange_(index);
    }
    if (onActivate_) {
        onActivate_(index);
    }
    return true;
}

} // namespace ned::ui
