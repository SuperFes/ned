#include "TreeView.h"

#include <algorithm>

#include "Border.h"
#include "KeyTranslation.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    bool IsQuit(const editor::KeyChord& chord) {
        return chord.Special == editor::SpecialKey::Escape || (chord.Control && chord.Codepoint == U'g');
    }

    // ListPopup.cpp's own PaintRowText, duplicated rather than shared --
    // small enough, and this codebase's own precedent elsewhere (e.g.
    // LspManagerTest.cpp/LspClientTest.cpp's ReadRawFrame) is to duplicate a
    // helper this size rather than add a new shared dependency for it.
    // Writes one codepoint per cell (not one byte per cell), and returns the
    // column x ended at.
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

    // The glyph shown in a row's disclosure column, per TreeRow's own doc
    // comment on hasChildren/expanded/loading's meaning.
    const char* DisclosureGlyph(const TreeRow& row) {
        if (row.loading) {
            return "…"; // "…" -- a request is in flight
        }
        if (!row.hasChildren) {
            return " "; // confirmed leaf -- no affordance at all
        }
        return row.expanded ? "▾" : "▸"; // "▾" / "▸"
    }

} // namespace

TreeView::TreeView(const Theme& theme) : theme_(theme) {
}

void TreeView::SetModel(TreeViewModel model) {
    model_ = std::move(model);
}

void TreeView::SetOnSelectionChanged(std::function<void(std::size_t)> onSelectionChanged) {
    onSelectionChanged_ = std::move(onSelectionChanged);
}

void TreeView::SetOnActivate(std::function<void(std::size_t)> onActivate) {
    onActivate_ = std::move(onActivate);
}

void TreeView::SetOnToggleExpand(std::function<void(std::size_t)> onToggleExpand) {
    onToggleExpand_ = std::move(onToggleExpand);
}

void TreeView::SetOnCollapseRequested(std::function<void(std::size_t)> onCollapseRequested) {
    onCollapseRequested_ = std::move(onCollapseRequested);
}

void TreeView::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void TreeView::Paint(Canvas c) {
    const int width  = c.size().width;
    const int height = c.size().height;
    if (width <= 0 || height <= 0) {
        return;
    }

    const Brush labelBrush{.background = theme_.background, .foreground = theme_.defaultForeground};
    const Brush selectedLabelBrush{.background = theme_.selectionBackground, .foreground = theme_.defaultForeground};
    const Brush glyphBrush{.background = theme_.background, .foreground = theme_.borderAccent.foreground, .bold = true};
    const Brush selectedGlyphBrush{
        .background = theme_.selectionBackground, .foreground = theme_.borderAccent.foreground, .bold = true};

    // Fill the interior before drawing anything else -- ListPopup's own
    // "otherwise the pane underneath bleeds through empty cells" fix,
    // confirmed live for that widget, applies identically here.
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
            break; // more rows than fit -- truncated, ListPopup's own overflow convention
        }
        const TreeRow& treeRow  = model_.rows[i];
        const bool     selected = model_.selectedIndex && *model_.selectedIndex == i;
        const Brush&   glyph    = selected ? selectedGlyphBrush : glyphBrush;
        const Brush&   text     = selected ? selectedLabelBrush : labelBrush;

        if (selected) {
            for (int x = 1; x < width - 1; ++x) {
                c[{.x = x, .y = row}].background_color = theme_.selectionBackground;
            }
        }

        // Indentation (2 columns per depth) + disclosure glyph, then the
        // label -- one column per codepoint throughout, PaintRowText's own
        // crude-but-consistent approximation (no grapheme-cluster/east-
        // asian-width accounting, matching ListPopup).
        const int indent = 2 + static_cast<int>(treeRow.depth) * 2;
        PaintRowText(c, indent, width, row, DisclosureGlyph(treeRow), glyph);
        PaintRowText(c, indent + 2, width, row, treeRow.label, text);
        ++row;
    }
}

bool TreeView::OnEvent(const Event& event) {
    if (event.is_mouse()) {
        return HandleMouseEvent(event);
    }
    if (!Focused()) {
        return false;
    }
    return HandleKeyEvent(event);
}

bool TreeView::HandleKeyEvent(const Event& event) {
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
        if (onSelectionChanged_) {
            onSelectionChanged_(selected);
        }
        return true;
    }

    if (chord->Special == editor::SpecialKey::Right) {
        const TreeRow& row = model_.rows[selected];
        if (row.hasChildren && !row.expanded && !row.loading && onToggleExpand_) {
            onToggleExpand_(selected);
        }
        return true;
    }

    if (chord->Special == editor::SpecialKey::Left) {
        const TreeRow& row = model_.rows[selected];
        if (row.hasChildren && row.expanded && onCollapseRequested_) {
            onCollapseRequested_(selected);
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

    return true; // every other key is consumed while this widget holds focus
}

bool TreeView::HandleMouseEvent(const Event& event) {
    const std::optional<MouseEvent> mouse = LocalMouseEvent(event);
    if (!mouse) {
        return true;
    }
    if (mouse->button != MouseEvent::Button::Left || mouse->motion != MouseEvent::Motion::Pressed) {
        return true;
    }

    const int row = mouse->at.y - 1; // row 0 (local y=0) is the top border
    if (row < 0 || static_cast<std::size_t>(row) >= model_.rows.size()) {
        return true;
    }

    const auto index     = static_cast<std::size_t>(row);
    model_.selectedIndex = index;
    if (onSelectionChanged_) {
        onSelectionChanged_(index);
    }
    if (onActivate_) {
        onActivate_(index);
    }
    return true;
}

} // namespace ned::ui
