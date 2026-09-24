//
// Spaces vs. tabs, and how many columns one indent level occupies. Lives in
// Text/ because a buffer can carry its own (Buffer::LocalIndentStyle); the
// configured defaults and per-mode table are Editor/IndentStyle.h's.
//

#ifndef NED_TEXT_INDENTSTYLE_H
#define NED_TEXT_INDENTSTYLE_H

namespace ned::text {

struct IndentStyle {
    bool useTabs = false; // default: spaces
    int  width   = 4;     // columns per indent level; also the spaces-per-tab-stop
                          // used when useTabs collapses a full-width run to a literal tab

    [[nodiscard]] bool operator==(const IndentStyle& other) const = default;
};

} // namespace ned::text

#endif // NED_TEXT_INDENTSTYLE_H
