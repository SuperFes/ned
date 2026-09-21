#include "TreeView.h"
#include "Paint.h"
#include "ThemePaints.h"

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
    // ManagerTest.cpp/ClientTest.cpp's ReadRawFrame) is to duplicate a
    // helper this size rather than add a new shared dependency for it.
    // Writes one codepoint per cell (not one byte per cell), and returns the
    // column x ended at.
    // Translucency phase 7: writes glyph/foreground/traits but not the
    // background, so the popup surface's fill survives underneath -- see
    // ListPopup's own copy for the full reasoning.
    int PaintRowText(Canvas& c, int x, int width, int row, const std::string& text, const Brush& brush) {
        std::size_t pos = 0;
        while (pos < text.size() && x < width - 1) {
            const std::size_t next = text::NextCodepointBoundary(text, pos);
            Cell&             cell = c[{.x = x, .y = row}];
            cell.character         = text.substr(pos, next - pos);
            brush.ApplyTextTo(cell);
            ++x;
            pos = next;
        }
        return x;
    }

    // Columns PaintRowText will consume for `text` -- one per codepoint,
    // the same crude approximation it paints with. ListPopup's own
    // DisplayColumnCount, duplicated for the reason above.
    int DisplayColumnCount(const std::string& text) {
        int         count = 0;
        std::size_t pos   = 0;
        while (pos < text.size()) {
            pos = text::NextCodepointBoundary(text, pos);
            ++count;
        }
        return count;
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

void TreeView::SetOnKey(std::function<void(const editor::KeyChord&)> onKey) {
    onKey_ = std::move(onKey);
}

void TreeView::SetDrawBorder(bool drawBorder) {
    drawBorder_ = drawBorder;
}

std::optional<std::size_t> TreeView::SelectedRow() const {
    if (model_.rows.empty()) {
        return std::nullopt;
    }
    return std::min(model_.selectedIndex.value_or(0), model_.rows.size() - 1);
}

void TreeView::EnsureSelectionVisible(int visibleRows) {
    if (visibleRows <= 0 || model_.rows.empty()) {
        scrollOffset_ = 0;
        return;
    }
    const auto        window    = static_cast<std::size_t>(visibleRows);
    const std::size_t maxOffset = model_.rows.size() > window ? model_.rows.size() - window : 0;
    scrollOffset_               = std::min(scrollOffset_, maxOffset);
    if (!model_.selectedIndex) {
        return;
    }
    const std::size_t selected = std::min(*model_.selectedIndex, model_.rows.size() - 1);
    if (selected < scrollOffset_) {
        scrollOffset_ = selected;
    }
    else if (selected >= scrollOffset_ + window) {
        scrollOffset_ = selected - window + 1;
    }
}

void TreeView::Paint(Canvas c) {
    const int width  = c.size().width;
    const int height = c.size().height;
    if (width <= 0 || height <= 0) {
        return;
    }

    const Brush labelBrush{.background = theme_.background, .foreground = theme_.defaultForeground};
    const Color selectionFill = OverlayBackground(theme_, SelectionFill(theme_));
    const Brush selectedLabelBrush{.background = selectionFill, .foreground = theme_.defaultForeground};
    const Brush glyphBrush{.background = theme_.background, .foreground = theme_.borderAccent.foreground, .bold = true};
    const Brush selectedGlyphBrush{
        .background = selectionFill, .foreground = theme_.borderAccent.foreground, .bold = true};

    // Fill the interior before drawing anything else -- ListPopup's own
    // "otherwise the pane underneath bleeds through empty cells" fix,
    // confirmed live for that widget, applies identically here.
    // Reset the interior's glyphs and traits. The background is handled
    // separately below, because whether it may be cleared depends on the
    // fill: a blur *samples* what is already there.
    // 1 when this widget owns its frame, 0 when a host does (see
    // SetDrawBorder) -- every row/column bound below is expressed against
    // it rather than against a hardcoded border of 1.
    const int inset = drawBorder_ ? 1 : 0;

    for (int y = inset; y < height - inset; ++y) {
        for (int x = inset; x < width - inset; ++x) {
            Cell& cell            = c[{.x = x, .y = y}];
            cell.character        = " ";
            labelBrush.ApplyTextTo(cell);
        }
    }

    // Translucency phase 7: same themed Surface ListPopup's body uses, so the
    // two popup shapes cannot drift apart. Derived default is the flat
    // background the loop above just wrote.
    const Surface surface = SurfaceFor(theme_, "popup");

    // A blur reads the destination, so its cells must keep whatever the tree
    // beneath this overlay painted this frame. Every other paint settles the
    // background itself -- except that a paint contributing nothing (the
    // derived default over a theme whose background is the terminal's own)
    // would leave the previous frame's cells showing, which is the stale-cell
    // bleed ListPopup's own interior fill has always existed to prevent.
    // Clearing first covers both: it reproduces the old behaviour exactly,
    // and a fill that does cover simply overwrites it.
    //
    // The cost is that a *translucent* fill composites against the theme's
    // background rather than against what the popup covers. Phase 7's
    // translucent-body step is where that gets revisited; blur is the case
    // that actually needs the destination today.
    const bool fillReadsDestination = PaintReadsDestination(surface.fill);
    if (!fillReadsDestination) {
        for (int y = inset; y < height - inset; ++y) {
            for (int x = inset; x < width - inset; ++x) {
                c[{.x = x, .y = y}].background_color = theme_.background;
            }
        }
    }
    const Point origin   = c.Origin();
    Canvas      interior = c.ForBox(Box{.x_min = origin.x + inset,
                                        .x_max = origin.x + width - 1 - inset,
                                        .y_min = origin.y + inset,
                                        .y_max = origin.y + height - 1 - inset});
    Fill(interior, surface.fill);

    if (drawBorder_) {
        DrawBorder(c, theme_.border);
        // Between the frame and its title on purpose -- see RecolourBorder.
        RecolourBorder(c, surface.border);
        DrawBorderTitle(c, model_.title, theme_.borderAccent);
    }

    EnsureSelectionVisible(height - 2 * inset);

    int row = inset;
    for (std::size_t i = scrollOffset_; i < model_.rows.size(); ++i) {
        if (row >= height - inset) {
            break; // past the bottom border -- the rest is reachable by scrolling
        }
        const TreeRow& treeRow  = model_.rows[i];
        const bool     selected = model_.selectedIndex && *model_.selectedIndex == i;
        const Brush&   glyph    = selected ? selectedGlyphBrush : glyphBrush;
        const Brush&   text     = selected ? selectedLabelBrush : labelBrush;

        if (selected) {
            for (int x = inset; x < width - inset; ++x) {
                c[{.x = x, .y = row}].background_color = selectionFill;
            }
        }

        // Indentation (2 columns per depth) + disclosure glyph, then the
        // label -- one column per codepoint throughout, PaintRowText's own
        // crude-but-consistent approximation (no grapheme-cluster/east-
        // asian-width accounting, matching ListPopup).
        const int indent = inset + 1 + static_cast<int>(treeRow.depth) * 2;
        const int rowEnd = width - inset + 1; // PaintRowText stops one short of its bound
        PaintRowText(c, indent, rowEnd, row, DisclosureGlyph(treeRow), glyph);
        // The trailing column is laid out first so the label can be clipped
        // against it -- ListPopup's own right-column rule, minus its
        // dim/accent styling choice, which a caller sets per row here.
        int labelLimit = rowEnd;
        if (!treeRow.right.empty()) {
            const int rightWidth  = DisplayColumnCount(treeRow.right);
            const int rightColumn = width - inset - rightWidth;
            if (rightColumn > indent + 2) {
                Brush rightBrush = text;
                if (!selected && treeRow.rightForeground) {
                    rightBrush.foreground = *treeRow.rightForeground;
                }
                PaintRowText(c, rightColumn, rowEnd, row, treeRow.right, rightBrush);
                labelLimit = rightColumn - 1;
            }
        }
        // The kind marker sits between the disclosure column and the label,
        // in its own per-kind color -- the selection brush still wins over
        // that color, same as the disclosure glyph above, so a selected row
        // stays legible rather than keeping a color chosen against the
        // unselected background.
        int labelColumn = indent + 2;
        if (!treeRow.kindGlyph.empty()) {
            Brush kindBrush = text;
            if (!selected && treeRow.kindForeground) {
                kindBrush.foreground = *treeRow.kindForeground;
            }
            PaintRowText(c, labelColumn, rowEnd, row, treeRow.kindGlyph, kindBrush);
            labelColumn += 2;
        }
        Brush labelText = text;
        if (!selected && treeRow.labelForeground) {
            labelText.foreground = *treeRow.labelForeground;
        }
        PaintRowText(c, labelColumn, labelLimit, row, treeRow.label, labelText);
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
        if (IsQuit(*chord)) {
            if (onCancel_) {
                onCancel_();
            }
        }
        else if (onKey_) {
            // Still offered: a consumer's row-independent action (add a
            // function breakpoint to an empty list) has to work before
            // there is anything to select.
            onKey_(*chord);
        }
        return true;
    }

    std::size_t selected = model_.selectedIndex.value_or(0);
    selected             = std::min(selected, model_.rows.size() - 1);

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

    if (onKey_) {
        onKey_(*chord);
    }
    return true; // every other key is consumed while this widget holds focus
}

bool TreeView::HandleMouseEvent(const Event& event) {
    const std::optional<MouseEvent> mouse = LocalMouseEvent(event);
    if (!mouse) {
        return true;
    }
    // A wheel tick moves the window without touching the selection -- the
    // document-like reading gesture, distinct from Up/Down's "move what I
    // am acting on" (ListPopup's own one-row-per-tick convention, scaled
    // to three here since this widget scrolls a list long enough to have
    // somewhere to go).
    if (mouse->button == MouseEvent::Button::WheelUp || mouse->button == MouseEvent::Button::WheelDown) {
        constexpr std::size_t kWheelRows = 3;
        if (mouse->button == MouseEvent::Button::WheelDown) {
            scrollOffset_ += kWheelRows;
        }
        else {
            scrollOffset_ = scrollOffset_ > kWheelRows ? scrollOffset_ - kWheelRows : 0;
        }
        // Clamped against the model here; Paint re-clamps against its own
        // height, which is the only thing that knows the real window.
        if (!model_.rows.empty()) {
            scrollOffset_ = std::min(scrollOffset_, model_.rows.size() - 1);
        }
        return true;
    }

    if (mouse->button != MouseEvent::Button::Left || mouse->motion != MouseEvent::Motion::Pressed) {
        return true;
    }

    const int inset = drawBorder_ ? 1 : 0;
    const int row   = mouse->at.y - inset; // with a frame, local y=0 is its top border
    // Bounded by this widget's own box rather than by the last Paint's
    // height: the box is current from the moment a layout assigns it, and a
    // click can legitimately arrive before any Paint has run.
    const int interiorRows = Box_().y_max - Box_().y_min + 1 - 2 * inset;
    if (row < 0 || row >= interiorRows) {
        return true;
    }
    const std::size_t index = scrollOffset_ + static_cast<std::size_t>(row);
    if (index >= model_.rows.size()) {
        return true;
    }

    model_.selectedIndex = index;
    if (onSelectionChanged_) {
        onSelectionChanged_(index);
    }
    // A click on the disclosure glyph itself is an expand/collapse gesture,
    // not an activation -- the affordance is drawn there, so clicking it
    // has to mean what it looks like. Everywhere else on the row still
    // activates.
    const TreeRow& treeRow          = model_.rows[index];
    const int      disclosureColumn = inset + 1 + static_cast<int>(treeRow.depth) * 2;
    if (mouse->at.x == disclosureColumn && treeRow.hasChildren && !treeRow.loading) {
        if (treeRow.expanded) {
            if (onCollapseRequested_) {
                onCollapseRequested_(index);
            }
        }
        else if (onToggleExpand_) {
            onToggleExpand_(index);
        }
        return true;
    }
    if (onActivate_) {
        onActivate_(index);
    }
    return true;
}

} // namespace ned::ui
