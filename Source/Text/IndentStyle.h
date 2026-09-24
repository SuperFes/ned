//
// Spaces vs. tabs, and how many columns one indent level occupies. Lives in
// Text/ because a buffer can carry its own (Buffer::LocalIndent); the
// configured defaults and per-mode table are Editor/IndentStyle.h's.
//

#ifndef NED_TEXT_INDENTSTYLE_H
#define NED_TEXT_INDENTSTYLE_H

#include <optional>

namespace ned::text {

struct IndentStyle {
    bool useTabs = false; // default: spaces
    int  width   = 4;     // columns per indent level; also the spaces-per-tab-stop
                          // used when useTabs collapses a full-width run to a literal tab

    [[nodiscard]] bool operator==(const IndentStyle& other) const = default;
};

// The part of a style one source states -- a modeline saying only
// `expandtab`, a file detected as tab-indented with no measurable width.
// Laid over a full style field by field.
struct IndentOverride {
    std::optional<bool> useTabs;
    std::optional<int>  width;

    [[nodiscard]] bool Empty() const {
        return !useTabs && !width;
    }

    [[nodiscard]] IndentStyle AppliedTo(IndentStyle base) const {
        if (useTabs) {
            base.useTabs = *useTabs;
        }
        if (width) {
            base.width = *width;
        }
        return base;
    }

    // `over`'s fields win where it has them.
    [[nodiscard]] IndentOverride OverlaidWith(const IndentOverride& over) const {
        return IndentOverride{.useTabs = over.useTabs ? over.useTabs : useTabs, .width = over.width ? over.width : width};
    }

    [[nodiscard]] bool operator==(const IndentOverride& other) const = default;
};

} // namespace ned::text

#endif // NED_TEXT_INDENTSTYLE_H
