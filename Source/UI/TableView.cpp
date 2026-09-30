#include "TableView.h"

#include <algorithm>
#include <iterator>

#include "Border.h"
#include "DrawText.h"
#include "KeyTranslation.h"
#include "Paint.h"
#include "Text/DisplayWidth.h"
#include "ThemePaints.h"

namespace ned::ui {

namespace {

    using table::Line;

    bool IsQuit(const editor::KeyChord& chord) {
        return chord.Special == editor::SpecialKey::Escape || (chord.Control && chord.Codepoint == U'g');
    }

    bool IsPlain(const editor::KeyChord& chord, char32_t codepoint) {
        return !chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint == codepoint;
    }

    // Text into [x, x + width), aligned, cut with an ellipsis when it
    // doesn't fit. Glyph and foreground only, so the row's background (the
    // selection bar, the surface fill) survives.
    void DrawCellText(Canvas& c, int x, int y, int width, std::string_view text, table::Align align, const Brush& brush) {
        if (width <= 0 || text.empty()) {
            return;
        }
        const int textWidth = text::StringColumns(text);
        if (textWidth <= width) {
            const int start = align == table::Align::Right ? x + width - textWidth : x;
            DrawText(c, start, y, text, brush, x + width, {.textOnly = true});
            return;
        }
        const std::size_t kept  = text::PrefixBytesForColumns(text, width - 1);
        const int         drawn = DrawText(c, x, y, text.substr(0, kept), brush, x + width - 1, {.textOnly = true});
        DrawText(c, x + drawn, y, "…", brush, x + width, {.textOnly = true});
    }

    const table::Cell& CellAt(const table::Row& row, std::size_t column) {
        static const table::Cell kEmpty;
        return column < row.cells.size() ? row.cells[column] : kEmpty;
    }

} // namespace

TableView::TableView(const Theme& theme) : theme_(theme) {
}

void TableView::SetModel(table::Model model) {
    const std::optional<LineKey> key = SelectedKey();
    model_                           = std::move(model);

    contentWidths_.assign(model_.columns.size(), 0);
    for (std::size_t i = 0; i < model_.columns.size(); ++i) {
        contentWidths_[i] = text::StringColumns(model_.columns[i].header);
    }
    for (const table::Group& group : model_.groups) {
        for (const table::Row& row : group.rows) {
            for (std::size_t i = 0; i < row.cells.size() && i < contentWidths_.size(); ++i) {
                contentWidths_[i] = std::max(contentWidths_[i], text::StringColumns(row.cells[i].text));
            }
        }
    }
    RebuildLines(key);
}

void TableView::SetDrawBorder(bool drawBorder) {
    drawBorder_ = drawBorder;
}

void TableView::SetDigitActivate(bool digitActivate) {
    digitActivate_ = digitActivate;
}

void TableView::SetSort(std::optional<table::Sort> sort) {
    const std::optional<LineKey> key = SelectedKey();
    sort_                            = std::move(sort);
    RebuildLines(key);
}

const std::optional<table::Sort>& TableView::CurrentSort() const {
    return sort_;
}

std::optional<std::string> TableView::SelectedRowId() const {
    if (selected_ >= lines_.size() || lines_[selected_].kind != Line::Kind::Row) {
        return std::nullopt;
    }
    const Line& line = lines_[selected_];
    return model_.groups[line.group].rows[line.row].id;
}

void TableView::SelectRow(std::string_view rowId) {
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        const Line& line = lines_[i];
        if (line.kind == Line::Kind::Row && model_.groups[line.group].rows[line.row].id == rowId) {
            selected_ = i;
            return;
        }
    }
}

void TableView::MoveSelection(int delta) {
    if (lines_.empty()) {
        return;
    }
    const auto last = static_cast<long long>(lines_.size() - 1);
    selected_       = static_cast<std::size_t>(std::clamp(static_cast<long long>(selected_) + delta, 0LL, last));
}

void TableView::SetOnActivate(std::function<void(const std::string&)> onActivate) {
    onActivate_ = std::move(onActivate);
}

void TableView::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void TableView::SetOnKey(std::function<void(const editor::KeyChord&)> onKey) {
    onKey_ = std::move(onKey);
}

void TableView::SetOnCellClick(std::function<bool(const std::string&, std::size_t, int)> onCellClick) {
    onCellClick_ = std::move(onCellClick);
}

std::optional<TableView::LineKey> TableView::SelectedKey() const {
    if (selected_ >= lines_.size()) {
        return std::nullopt;
    }
    const Line&         line  = lines_[selected_];
    const table::Group& group = model_.groups[line.group];
    if (line.kind == Line::Kind::GroupHeader) {
        return LineKey{.kind = Line::Kind::GroupHeader, .id = group.id};
    }
    return LineKey{.kind = Line::Kind::Row, .id = group.rows[line.row].id};
}

void TableView::RebuildLines(const std::optional<LineKey>& key) {
    lines_ = table::BuildLines(model_, sort_, collapsedGroups_);
    if (key) {
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            const Line&         line  = lines_[i];
            const table::Group& group = model_.groups[line.group];
            const std::string&  id    = line.kind == Line::Kind::GroupHeader ? group.id : group.rows[line.row].id;
            if (line.kind == key->kind && id == key->id) {
                selected_ = i;
                return;
            }
        }
    }
    selected_ = lines_.empty() ? 0 : std::min(selected_, lines_.size() - 1);
}

int TableView::Inset() const {
    return drawBorder_ ? 1 : 0;
}

bool TableView::Grouped() const {
    return std::ranges::any_of(model_.groups, [](const table::Group& group) { return !group.id.empty(); });
}

int TableView::FirstLineY() const {
    return Inset() + (model_.columns.empty() ? 0 : 1);
}

int TableView::VisibleLines(int height) const {
    return std::max(0, height - Inset() - FirstLineY());
}

std::vector<table::ColumnSlot> TableView::Slots(int width, int& columnsX) const {
    // One blank cell of padding each side; grouped rows sit under their
    // header's disclosure glyph.
    columnsX            = Inset() + 1 + (Grouped() ? 2 : 0);
    const int available = width - Inset() - 1 - columnsX;
    // The sorted column's header needs room for its arrow.
    std::vector<int> widths = contentWidths_;
    if (sort_) {
        const auto found = std::ranges::find(model_.columns, sort_->columnId, &table::Column::id);
        if (found != model_.columns.end()) {
            const auto index = static_cast<std::size_t>(found - model_.columns.begin());
            widths[index]    = std::max(widths[index], text::StringColumns(found->header) + 2);
        }
    }
    return table::LayoutColumns(model_.columns, widths, available);
}

void TableView::EnsureSelectionVisible(int visibleLines) {
    if (visibleLines <= 0 || lines_.empty()) {
        scrollOffset_ = 0;
        return;
    }
    const auto        window    = static_cast<std::size_t>(visibleLines);
    const std::size_t maxOffset = lines_.size() > window ? lines_.size() - window : 0;
    scrollOffset_               = std::min(scrollOffset_, maxOffset);
    if (selected_ < scrollOffset_) {
        scrollOffset_ = selected_;
    }
    else if (selected_ >= scrollOffset_ + window) {
        scrollOffset_ = selected_ - window + 1;
    }
}

void TableView::Paint(Canvas c) {
    const int width  = c.size().width;
    const int height = c.size().height;
    if (width <= 0 || height <= 0) {
        return;
    }
    const int inset = Inset();

    const Color selectionFill = OverlayBackground(theme_, SelectionFill(theme_));
    const Brush textBrush{.background = theme_.background, .foreground = theme_.defaultForeground};
    const Brush glyphBrush{.background = theme_.background, .foreground = theme_.borderAccent.foreground, .bold = true};
    const Brush dimBrush{.background = theme_.background, .foreground = theme_.indentGuideForeground};
    const Brush headerBrush{.background = theme_.background, .foreground = theme_.commentForeground, .bold = true};

    // TreeView::Paint's interior fill and surface, for the same reasons:
    // stale cells must not bleed through, and a blur samples what is
    // underneath, so it mustn't be cleared first.
    for (int y = inset; y < height - inset; ++y) {
        for (int x = inset; x < width - inset; ++x) {
            Cell& cell     = c[{.x = x, .y = y}];
            cell.character = " ";
            textBrush.ApplyTextTo(cell);
        }
    }
    const Surface surface = SurfaceFor(theme_, "popup");
    if (!PaintReadsDestination(surface.fill)) {
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
        RecolourBorder(c, surface.border);
        DrawBorderTitle(c, model_.title, theme_.borderAccent);
    }

    int                                  columnsX = 0;
    const std::vector<table::ColumnSlot> slots    = Slots(width, columnsX);
    const int                            rowEnd   = width - inset - 1; // exclusive
    const int                            baseX    = inset + 1;

    if (!model_.columns.empty() && inset < height - inset) {
        for (const table::ColumnSlot& slot : slots) {
            const table::Column& column = model_.columns[slot.column];
            const bool           sorted = sort_ && sort_->columnId == column.id;
            Brush                brush  = headerBrush;
            if (sorted) {
                brush.foreground = theme_.borderAccent.foreground;
            }
            const int x = columnsX + slot.x;
            DrawCellText(c, x, inset, slot.width, column.header, column.align, brush);
            if (sorted) {
                // Beside the header when there's room, over its end when not.
                const int headerWidth = text::StringColumns(column.header);
                const int offset      = column.align == table::Align::Right ? std::max(0, slot.width - headerWidth - 2)
                                                                            : std::min(headerWidth + 1, slot.width - 1);
                DrawText(c, x + offset, inset, sort_->descending ? "▾" : "▴", brush, x + slot.width, {.textOnly = true});
            }
        }
    }

    const int firstY  = FirstLineY();
    const int visible = VisibleLines(height);
    if (visible <= 0) {
        return;
    }

    if (lines_.empty()) {
        for (int x = inset; x < width - inset; ++x) {
            c[{.x = x, .y = firstY}].background_color = selectionFill;
        }
        DrawCellText(c, baseX, firstY, rowEnd - baseX, model_.placeholder, table::Align::Left, dimBrush);
        return;
    }

    EnsureSelectionVisible(visible);
    for (int i = 0; i < visible && scrollOffset_ + static_cast<std::size_t>(i) < lines_.size(); ++i) {
        const std::size_t   index    = scrollOffset_ + static_cast<std::size_t>(i);
        const Line&         line     = lines_[index];
        const table::Group& group    = model_.groups[line.group];
        const bool          selected = index == selected_;
        const int           y        = firstY + i;
        if (selected) {
            for (int x = inset; x < width - inset; ++x) {
                c[{.x = x, .y = y}].background_color = selectionFill;
            }
        }

        if (line.kind == Line::Kind::GroupHeader) {
            const char* glyph = model_.loading                        ? "…"
                                : collapsedGroups_.contains(group.id) ? "▸"
                                                                      : "▾";
            DrawText(c, baseX, y, glyph, glyphBrush, rowEnd, {.textOnly = true});
            int labelEnd = rowEnd;
            if (!group.right.empty()) {
                const int rightWidth = text::StringColumns(group.right);
                if (rowEnd - rightWidth > baseX + 3) {
                    DrawCellText(c, rowEnd - rightWidth, y, rightWidth, group.right, table::Align::Right, dimBrush);
                    labelEnd = rowEnd - rightWidth - 1;
                }
            }
            DrawCellText(c, baseX + 2, y, labelEnd - baseX - 2, group.label, table::Align::Left, textBrush);
            continue;
        }

        const table::Row& row = group.rows[line.row];
        for (const table::ColumnSlot& slot : slots) {
            const table::Cell& cell  = CellAt(row, slot.column);
            Brush              brush = textBrush;
            if (!selected && cell.foreground) {
                brush.foreground = *cell.foreground;
            }
            brush.bold = cell.bold;
            DrawCellText(c, columnsX + slot.x, y, slot.width, cell.text, model_.columns[slot.column].align, brush);
        }
    }
}

void TableView::CycleSort() {
    std::vector<std::size_t> sortable;
    for (std::size_t i = 0; i < model_.columns.size(); ++i) {
        if (model_.columns[i].sortable) {
            sortable.push_back(i);
        }
    }
    if (sortable.empty()) {
        return;
    }
    auto target = sortable.begin();
    if (sort_) {
        const auto current =
            std::ranges::find_if(sortable, [&](std::size_t i) { return model_.columns[i].id == sort_->columnId; });
        if (current != sortable.end()) {
            target = std::next(current);
        }
    }
    std::optional<table::Sort> next;
    if (target != sortable.end()) {
        const table::Column& column = model_.columns[*target];
        next                        = table::Sort{.columnId = column.id, .descending = column.descendingFirst};
    }
    SetSort(std::move(next));
}

void TableView::SortByColumn(std::size_t column) {
    if (column >= model_.columns.size() || !model_.columns[column].sortable) {
        return;
    }
    const table::Column& target = model_.columns[column];
    if (sort_ && sort_->columnId == target.id) {
        SetSort(table::Sort{.columnId = target.id, .descending = !sort_->descending});
    }
    else {
        SetSort(table::Sort{.columnId = target.id, .descending = target.descendingFirst});
    }
}

void TableView::SetGroupCollapsed(std::size_t group, bool collapsed) {
    const std::string& id = model_.groups[group].id;
    if (id.empty()) {
        return;
    }
    std::optional<LineKey> key = SelectedKey();
    if (collapsed) {
        collapsedGroups_.insert(id);
        // Closing from inside a group lands on its header rather than on
        // whatever slides up under the selection.
        key = LineKey{.kind = Line::Kind::GroupHeader, .id = id};
    }
    else {
        collapsedGroups_.erase(id);
    }
    RebuildLines(key);
}

void TableView::Activate(std::size_t index) {
    if (index >= lines_.size()) {
        return;
    }
    selected_        = index;
    const Line& line = lines_[index];
    if (line.kind == Line::Kind::GroupHeader) {
        SetGroupCollapsed(line.group, !collapsedGroups_.contains(model_.groups[line.group].id));
        return;
    }
    if (onActivate_) {
        onActivate_(model_.groups[line.group].rows[line.row].id);
    }
}

bool TableView::OnEvent(const Event& event) {
    if (event.is_mouse()) {
        return HandleMouseEvent(event);
    }
    if (!Focused()) {
        return false;
    }
    return HandleKeyEvent(event);
}

bool TableView::HandleKeyEvent(const Event& event) {
    const auto chord = TranslateKey(event);
    if (!chord) {
        return true; // focused: swallow undecodable input rather than leaking it
    }
    using editor::SpecialKey;

    if (IsQuit(*chord)) {
        if (onCancel_) {
            onCancel_();
        }
        return true;
    }
    if (IsPlain(*chord, U'S')) {
        CycleSort();
        return true;
    }
    if (IsPlain(*chord, U'R')) {
        if (sort_) {
            SetSort(table::Sort{.columnId = sort_->columnId, .descending = !sort_->descending});
        }
        return true;
    }

    if (lines_.empty()) {
        if (chord->Special == SpecialKey::Enter) {
            if (onActivate_) {
                onActivate_(std::string());
            }
        }
        else if (onKey_) {
            // Row-independent actions (refresh) have to work before there is
            // anything to select.
            onKey_(*chord);
        }
        return true;
    }

    const std::size_t count = lines_.size();
    const bool        up    = chord->Special == SpecialKey::Up || (chord->Control && chord->Codepoint == U'p');
    const bool        down  = chord->Special == SpecialKey::Down || (chord->Control && chord->Codepoint == U'n');
    if (up || down) {
        selected_ = down ? (selected_ + 1) % count : (selected_ + count - 1) % count;
        return true;
    }
    if (chord->Special == SpecialKey::PageUp || chord->Special == SpecialKey::PageDown) {
        const int page = std::max(1, VisibleLines(Box_().y_max - Box_().y_min + 1) - 1);
        MoveSelection(chord->Special == SpecialKey::PageDown ? page : -page);
        return true;
    }
    if (chord->Special == SpecialKey::Home || chord->Special == SpecialKey::End) {
        selected_ = chord->Special == SpecialKey::Home ? 0 : count - 1;
        return true;
    }

    const Line& line = lines_[selected_];
    if (chord->Special == SpecialKey::Right) {
        if (line.kind == Line::Kind::GroupHeader) {
            SetGroupCollapsed(line.group, false);
        }
        return true;
    }
    if (chord->Special == SpecialKey::Left) {
        if (line.kind == Line::Kind::GroupHeader) {
            SetGroupCollapsed(line.group, true);
        }
        else if (const std::string& id = model_.groups[line.group].id; !id.empty()) {
            RebuildLines(LineKey{.kind = Line::Kind::GroupHeader, .id = id});
        }
        return true;
    }
    if (chord->Special == SpecialKey::Enter) {
        Activate(selected_);
        return true;
    }
    if (digitActivate_ && !chord->Control && !chord->Meta && chord->Codepoint >= U'1' && chord->Codepoint <= U'9') {
        std::size_t wanted = chord->Codepoint - U'1';
        for (std::size_t i = 0; i < count; ++i) {
            if (lines_[i].kind == Line::Kind::Row && wanted-- == 0) {
                Activate(i);
                break;
            }
        }
        return true;
    }

    if (onKey_) {
        onKey_(*chord);
    }
    return true; // every other key is consumed while this widget holds focus
}

bool TableView::HandleMouseEvent(const Event& event) {
    const std::optional<MouseEvent> mouse = LocalMouseEvent(event);
    if (!mouse) {
        return true;
    }
    // TreeView's wheel: moves the window, not the selection.
    if (mouse->button == MouseEvent::Button::WheelUp || mouse->button == MouseEvent::Button::WheelDown) {
        constexpr std::size_t kWheelLines = 3;
        if (mouse->button == MouseEvent::Button::WheelDown) {
            scrollOffset_ += kWheelLines;
        }
        else {
            scrollOffset_ = scrollOffset_ > kWheelLines ? scrollOffset_ - kWheelLines : 0;
        }
        if (!lines_.empty()) {
            scrollOffset_ = std::min(scrollOffset_, lines_.size() - 1);
        }
        return true;
    }
    if (mouse->button != MouseEvent::Button::Left || mouse->motion != MouseEvent::Motion::Pressed) {
        return true;
    }

    // Against the box rather than the last Paint: a click can arrive before
    // any Paint has run.
    const int width  = Box_().x_max - Box_().x_min + 1;
    const int height = Box_().y_max - Box_().y_min + 1;

    int                                  columnsX = 0;
    const std::vector<table::ColumnSlot> slots    = Slots(width, columnsX);
    const auto                           slotAt   = [&](int x) -> const table::ColumnSlot* {
        const auto found = std::ranges::find_if(slots, [&](const table::ColumnSlot& slot) {
            return x >= columnsX + slot.x && x < columnsX + slot.x + slot.width;
        });
        return found == slots.end() ? nullptr : &*found;
    };

    if (!model_.columns.empty() && mouse->at.y == Inset()) {
        if (const table::ColumnSlot* slot = slotAt(mouse->at.x)) {
            SortByColumn(slot->column);
        }
        return true;
    }

    const int lineRow = mouse->at.y - FirstLineY();
    if (lineRow < 0 || lineRow >= VisibleLines(height)) {
        return true;
    }
    if (lines_.empty()) {
        if (lineRow == 0 && onActivate_) {
            onActivate_(std::string());
        }
        return true;
    }
    const std::size_t index = scrollOffset_ + static_cast<std::size_t>(lineRow);
    if (index >= lines_.size()) {
        return true;
    }
    selected_ = index;

    const Line& line = lines_[index];
    if (line.kind == Line::Kind::Row && onCellClick_) {
        if (const table::ColumnSlot* slot = slotAt(mouse->at.x)) {
            const std::string& id = model_.groups[line.group].rows[line.row].id;
            if (onCellClick_(id, slot->column, mouse->at.x - columnsX - slot->x)) {
                return true;
            }
        }
    }
    Activate(index);
    return true;
}

} // namespace ned::ui
