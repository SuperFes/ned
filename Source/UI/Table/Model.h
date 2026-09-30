//
// What a TableView shows: columns, and rows of cells optionally gathered
// under collapsible group headers. Plain data a panel rebuilds and pushes on
// every change; sort order, collapsed groups, selection and scroll are the
// widget's own and survive a push, matched by id.
//

#ifndef NED_UI_TABLE_MODEL_H
#define NED_UI_TABLE_MODEL_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "UI/Widget.h"

namespace ned::ui::table {

enum class Align { Left,
                   Right };

struct Column {
    std::string id; // stable across pushes: the sort follows it
    std::string header;

    // Fit takes the widest of header and cells, clamped to [minWidth,
    // maxWidth]; Flex takes whatever the Fit columns leave, never less than
    // minWidth. More than one Flex column share the remainder evenly.
    enum class Width { Fit,
                       Flex };
    Width width    = Width::Fit;
    int   minWidth = 1;
    int   maxWidth = 40;

    Align align = Align::Left;

    // 0 is never dropped. Otherwise, when the columns don't fit, the
    // highest value goes first.
    int dropPriority = 0;

    bool sortable = true;
    // Recency, size and count columns read best largest-first.
    bool descendingFirst = false;
};

struct Cell {
    std::string          text;
    std::optional<Color> foreground; // the selection brush still wins
    bool                 bold = false;
    // Compared instead of text when both cells being compared have one.
    std::optional<std::int64_t> sortNumber;
};

struct Row {
    std::string       id; // non-empty, unique within the model
    std::vector<Cell> cells;
};

struct Group {
    // An empty id is a headerless group: its rows are always shown, and a
    // table that is one such group is a plain flat list.
    std::string      id;
    std::string      label;
    std::string      right; // right-aligned on the header row (a count)
    std::vector<Row> rows;
};

struct Model {
    std::string         title; // the border title, when the widget draws one
    std::vector<Column> columns;
    std::vector<Group>  groups;
    // Drawn dimmed when there are no rows at all ("Loading…").
    std::string placeholder;
    // Shown on every group header's disclosure glyph while a refetch runs.
    bool loading = false;
};

} // namespace ned::ui::table

#endif // NED_UI_TABLE_MODEL_H
