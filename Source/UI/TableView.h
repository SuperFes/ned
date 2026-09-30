//
// A persistent, sortable, column-aligned list of records (UI/Table/Model.h),
// optionally gathered under collapsible group headers. Where TreeView is a
// hierarchy and ListPopup a transient pick-list, this is the record view a
// panel keeps open: issues, buffers, threads.
//
// The panel owns the records and pushes a fresh Model whenever they change.
// The widget owns everything about viewing them -- sort, collapsed groups,
// selection, scroll -- keyed by column, group and row id, so all of it
// survives a push. Per-row actions stay the panel's: a key the widget
// doesn't use goes to SetOnKey, and the panel asks SelectedRowId() which
// record it applies to.
//
// Keys: Up/Down/C-p/C-n (wrapping), PageUp/PageDown, Home/End; Right opens a
// group, Left closes it or climbs from a row to its group's header; Enter
// toggles a group or activates a row; S sorts by the next column (the last
// step returns to the model's own order) and R reverses the sort;
// Escape/C-g cancels. Digits 1-9 select and activate the Nth row when
// SetDigitActivate is on. Mouse: a header click sorts by that column (again
// to reverse), a click toggles a group or activates a row, the wheel
// scrolls.
//

#ifndef NED_UI_TABLEVIEW_H
#define NED_UI_TABLEVIEW_H

#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Key.h"
#include "Table/Layout.h"
#include "Table/Model.h"
#include "Table/Order.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class TableView : public Widget {
  public:
    explicit TableView(const Theme& theme);

    void SetModel(table::Model model);

    [[nodiscard]] bool Focusable() const override {
        return true;
    }

    // False when a host (LeftDock) draws the frame and title.
    void SetDrawBorder(bool drawBorder);
    // ListPopup's one-keystroke pick, for short lists.
    void SetDigitActivate(bool digitActivate);

    // std::nullopt is the model's own order.
    void                                            SetSort(std::optional<table::Sort> sort);
    [[nodiscard]] const std::optional<table::Sort>& CurrentSort() const;

    // The selected record; std::nullopt on a group header or an empty table.
    [[nodiscard]] std::optional<std::string> SelectedRowId() const;
    // No-op for an id the model doesn't show (a row in a collapsed group).
    void SelectRow(std::string_view rowId);
    // Clamped at either end rather than wrapping: a panel stepping past the
    // row it just acted on (dired's mark-and-advance) stays on the last one.
    void MoveSelection(int delta);

    // Enter or a click on a row. An empty id is the placeholder, so a
    // "g to retry" line can be acted on.
    void SetOnActivate(std::function<void(const std::string& rowId)> onActivate);
    void SetOnCancel(std::function<void()> onCancel);
    void SetOnKey(std::function<void(const editor::KeyChord&)> onKey);
    // A click on a row's cell, before activation: column indexes
    // Model::columns and offset is the cell-relative column clicked. Return
    // true to consume the click (it still selects the row).
    void SetOnCellClick(std::function<bool(const std::string& rowId, std::size_t column, int offset)> onCellClick);

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;

  private:
    const Theme&               theme_;
    table::Model               model_;
    std::vector<int>           contentWidths_; // per column: widest of header and cells
    std::optional<table::Sort> sort_;
    std::set<std::string>      collapsedGroups_;
    std::vector<table::Line>   lines_;
    std::size_t                selected_      = 0; // into lines_
    std::size_t                scrollOffset_  = 0; // first line shown
    bool                       drawBorder_    = true;
    bool                       digitActivate_ = false;

    std::function<void(const std::string&)>                          onActivate_;
    std::function<void()>                                            onCancel_;
    std::function<void(const editor::KeyChord&)>                     onKey_;
    std::function<bool(const std::string&, std::size_t column, int)> onCellClick_;

    // A selected line by identity, so it can be found again after lines_
    // is rebuilt: the group's id for a header, the row's id for a row.
    struct LineKey {
        table::Line::Kind kind;
        std::string       id;
    };
    [[nodiscard]] std::optional<LineKey> SelectedKey() const;
    // Rebuilds lines_ after a push, a sort or a collapse, selecting `key`
    // when it is still shown and otherwise keeping the selected position.
    void RebuildLines(const std::optional<LineKey>& key);

    [[nodiscard]] int                            Inset() const;
    [[nodiscard]] bool                           Grouped() const;
    [[nodiscard]] int                            FirstLineY() const; // below the column header
    [[nodiscard]] int                            VisibleLines(int height) const;
    [[nodiscard]] std::vector<table::ColumnSlot> Slots(int width, int& columnsX) const;

    void CycleSort();
    void SortByColumn(std::size_t column);
    void SetGroupCollapsed(std::size_t group, bool collapsed);
    void Activate(std::size_t line);
    void EnsureSelectionVisible(int visibleLines);

    bool HandleKeyEvent(const Event& event);
    bool HandleMouseEvent(const Event& event);
};

} // namespace ned::ui

#endif // NED_UI_TABLEVIEW_H
