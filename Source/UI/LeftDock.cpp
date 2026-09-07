#include "LeftDock.h"

#include <algorithm>

#include "Border.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {
constexpr int kRailWidth          = 3; // one glyph, centered, plus one column of padding each side
constexpr int kMinContentWidth    = 4; // ProjectSidebar::kMinSidebarWidth's own value
constexpr int kMinWidth           = kRailWidth + kMinContentWidth;
constexpr int kHeaderHeight       = 1; // the content region's own border title row
constexpr int kBottomBorderHeight = 1;
} // namespace

LeftDock::LeftDock(const Theme& theme) : theme_(theme) {
}

std::size_t LeftDock::AddPanel(char32_t glyph, std::string name, Widget& content) {
    const std::size_t id = nextId_++;
    entries_.push_back(Entry{.id = id, .glyph = glyph, .name = std::move(name), .content = &content});
    if (entries_.size() == 1) {
        active_ = id; // the first registered panel starts active
    }
    return id;
}

LeftDock::Entry* LeftDock::FindEntry(std::size_t id) {
    for (Entry& entry : entries_) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

const LeftDock::Entry* LeftDock::FindEntry(std::size_t id) const {
    for (const Entry& entry : entries_) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

LeftDock::Entry* LeftDock::FindEntryByContent(Widget* content) {
    for (Entry& entry : entries_) {
        if (entry.content == content) {
            return &entry;
        }
    }
    return nullptr;
}

Widget* LeftDock::ActiveContent() const {
    const Entry* entry = FindEntry(active_);
    return entry != nullptr ? entry->content : nullptr;
}

void LeftDock::SwitchTo(std::size_t id) {
    if (FindEntry(id) == nullptr) {
        return;
    }
    active_ = id;
    RepositionActiveContent();
}

void LeftDock::CommitSwitchTo(std::size_t id) {
    if (FindEntry(id) == nullptr) {
        return;
    }
    SwitchTo(id);
    if (onActivePanelCommitted_) {
        onActivePanelCommitted_(active_);
    }
}

void LeftDock::ActivateOrToggle(Widget* content) {
    const Entry* entry = FindEntryByContent(content);
    if (entry == nullptr) {
        return;
    }
    const std::size_t id = entry->id; // entry can dangle after CommitSwitchTo/CommitCollapsed reposition content
    if (!collapsed_ && id == active_) {
        CommitCollapsed(true); // VS Code's own convention: re-clicking the active glyph collapses
    }
    else {
        if (id != active_) {
            CommitSwitchTo(id);
        }
        if (collapsed_) {
            CommitCollapsed(false);
        }
    }
}

int LeftDock::Width() const {
    // Collapsed reports just the rail; width_ itself is preserved so
    // expanding restores the previous width exactly (ProjectSidebar::
    // Width()'s own contract).
    return collapsed_ ? kRailWidth : width_;
}

void LeftDock::SetWidth(int width) {
    width_ = std::max(kMinWidth, width);
}

int LeftDock::ExpandedWidth() const {
    return width_;
}

bool LeftDock::Collapsed() const {
    return collapsed_;
}

void LeftDock::SetCollapsed(bool collapsed) {
    collapsed_ = collapsed;
    if (collapsed_ && resizing_) {
        EndResize(); // a resize session can't meaningfully outlive the frame it was resizing
    }
    if (collapsed_) {
        // Collapsing while the active panel's own content widget holds
        // keyboard focus would otherwise leave the keyboard captured by
        // something nothing can see or reach anymore -- Widget::
        // OnFocusPreempted's own doc comment. Checked on every collapse,
        // not just a genuine expanded->collapsed transition, matching
        // ProjectSidebar's own former SetCollapsed(true) precedent.
        if (Widget* content = ActiveContent(); content != nullptr && content->Focused()) {
            content->OnFocusPreempted();
        }
    }
    RepositionActiveContent(); // no-op while still collapsed; re-establishes the box on expand
}

void LeftDock::ToggleCollapsed() {
    CommitCollapsed(!collapsed_);
}

void LeftDock::PrepareForKeyboardFocus(Widget* content) {
    const Entry* entry = FindEntryByContent(content);
    if (entry == nullptr) {
        return;
    }
    if (entry->id != active_) {
        SwitchTo(entry->id); // silent -- a quick keyboard visit shouldn't overwrite the remembered active panel
    }
    collapseOnFocusReturn_ = collapsed_;
    SetCollapsed(false);
}

void LeftDock::NoteFocusReturned() {
    const bool recollapse  = collapseOnFocusReturn_;
    collapseOnFocusReturn_ = false;
    if (recollapse) {
        SetCollapsed(true);
    }
}

void LeftDock::CommitCollapsed(bool collapsed) {
    SetCollapsed(collapsed);
    if (onCollapseCommitted_) {
        onCollapseCommitted_(collapsed_);
    }
}

bool LeftDock::IsResizing() const {
    return resizing_;
}

void LeftDock::BeginResize(int globalMouseX) {
    resizing_            = true;
    resizeAnchorGlobalX_ = globalMouseX;
    // Anchored to size().width (the box this widget is actually currently
    // rendered at), not width_ directly -- ProjectSidebar::BeginResize's own
    // staleness-avoidance precedent (see that method's own comment): the two
    // can disagree for a widget a test constructed and SetBox_'d directly
    // without ever going through a real per-frame Width()-reading layout
    // pass first.
    resizeAnchorWidth_ = size().width;
    resizeStartWidth_  = width_;
}

void LeftDock::UpdateResize(int globalMouseX) {
    // Anchored to the drag's total displacement from its start, not applied
    // as a per-event delta -- ProjectSidebar::UpdateResize's own reasoning
    // (a growing drag can hand off from this widget's own OnEvent to
    // whichever widget now has the cursor, so there's no single consistent
    // "previous event" to diff against across that handoff).
    const int delta = globalMouseX - resizeAnchorGlobalX_;
    width_           = std::max(kMinWidth, resizeAnchorWidth_ + delta);
}

void LeftDock::EndResize() {
    if (!resizing_) {
        return;
    }
    resizing_ = false;
    if (width_ != resizeStartWidth_ && onWidthCommitted_) {
        onWidthCommitted_(width_);
    }
}

void LeftDock::SetOnWidthCommitted(std::function<void(int)> handler) {
    onWidthCommitted_ = std::move(handler);
}

void LeftDock::SetOnCollapseCommitted(std::function<void(bool)> handler) {
    onCollapseCommitted_ = std::move(handler);
}

void LeftDock::SetOnActivePanelCommitted(std::function<void(std::size_t)> handler) {
    onActivePanelCommitted_ = std::move(handler);
}

Box LeftDock::ContentBox() const {
    const Box& own = Box_();
    return Box{.x_min = own.x_min + kRailWidth, .x_max = own.x_max, .y_min = own.y_min, .y_max = own.y_max};
}

Box LeftDock::ContentInteriorBox() const {
    const Box content = ContentBox();
    return Box{.x_min = content.x_min + 1,
               .x_max = content.x_max - 1,
               .y_min = content.y_min + kHeaderHeight,
               .y_max = content.y_max - kBottomBorderHeight};
}

void LeftDock::RepositionActiveContent() {
    if (collapsed_ || entries_.empty()) {
        return;
    }
    if (Widget* content = ActiveContent()) {
        content->SetBox_(ContentInteriorBox());
    }
}

void LeftDock::OnResize(Size /*previous*/) {
    RepositionActiveContent();
}

void LeftDock::Paint(Canvas c) {
    if (c.size().width <= 0 || c.size().height <= 0) {
        return;
    }

    // The Screen isn't cleared between frames (only recreated on resize),
    // so a stale cell from a previous, wider frame would otherwise bleed
    // through -- ProjectSidebar::Paint's own header comment, same
    // underlying Screen-reuse contract.
    const Brush blankBrush{.background = theme_.background, .foreground = theme_.defaultForeground};
    for (int row = 0; row < c.size().height; ++row) {
        for (int col = 0; col < c.size().width; ++col) {
            Cell& cell     = c[{.x = col, .y = row}];
            cell.character = " ";
            blankBrush.ApplyTo(cell);
        }
    }

    // The rail: one row per registered panel, always painted regardless of
    // Collapsed() -- it's the click target that both switches and expands
    // (see this file's own header comment on why collapse is rail-driven,
    // not divider-driven).
    for (std::size_t i = 0; i < entries_.size() && static_cast<int>(i) < c.size().height; ++i) {
        const Entry& entry    = entries_[i];
        const bool   isActive = entry.id == active_;
        const Brush& railBrush = isActive ? theme_.activeTab : theme_.tabBar;
        const int    row       = static_cast<int>(i);
        for (int col = 0; col < kRailWidth && col < c.size().width; ++col) {
            Cell& cell     = c[{.x = col, .y = row}];
            cell.character = " ";
            railBrush.ApplyTo(cell);
        }
        if (1 < c.size().width) {
            Cell& glyphCell     = c[{.x = 1, .y = row}];
            glyphCell.character = text::EncodeCodepointUtf8(entry.glyph);
            railBrush.ApplyTo(glyphCell);
        }
    }

    if (collapsed_ || entries_.empty()) {
        return;
    }

    const Entry* activeEntry = FindEntry(active_);
    if (activeEntry == nullptr) {
        return;
    }

    Canvas       contentCanvas = c.ForBox(ContentBox());
    const Brush& frameBrush    = resizing_ ? theme_.borderAccent : theme_.border;
    DrawBorder(contentCanvas, frameBrush);
    DrawBorderTitle(contentCanvas, activeEntry->name, theme_.borderAccent);

    RepositionActiveContent();
    if (Widget* content = activeEntry->content) {
        Canvas interiorCanvas = c.ForBox(ContentInteriorBox());
        content->Paint(interiorCanvas);
    }
}

bool LeftDock::OnEvent(const Event& event) {
    if (!event.is_mouse()) {
        return false; // no keyboard plumbing through this class -- see this file's own header comment
    }
    const MouseEvent rawMouse = event.mouse();

    // ProjectSidebar/PanelDock's own resize-drag shape: Moved/Released are
    // handled against the *global* mouse position before the local-bounds
    // hit test below, since a fast drag can carry the cursor outside this
    // widget's own Box mid-session.
    if (rawMouse.motion == MouseEvent::Motion::Moved && resizing_) {
        UpdateResize(rawMouse.at.x);
        return true;
    }
    if (rawMouse.motion == MouseEvent::Motion::Released && resizing_) {
        EndResize();
        return true;
    }

    const std::optional<MouseEvent> mouse = LocalMouseEvent(event);
    if (!mouse) {
        return false;
    }

    if (mouse->at.x < kRailWidth) {
        if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed &&
            mouse->at.y >= 0 && static_cast<std::size_t>(mouse->at.y) < entries_.size()) {
            ActivateOrToggle(entries_[static_cast<std::size_t>(mouse->at.y)].content);
        }
        return true; // the rail consumes every event inside its own columns, whether or not it matched a row
    }

    if (!collapsed_ && mouse->at.x == size().width - 1 && mouse->button == MouseEvent::Button::Left &&
        mouse->motion == MouseEvent::Motion::Pressed) {
        BeginResize(rawMouse.at.x);
        return true;
    }

    // Anything else -- while expanded -- forwards unmodified (still global
    // coordinates) to the active panel's own OnEvent, which does its own
    // LocalMouseEvent translation against its own Box_() exactly as it does
    // standalone (PanelDock::OnEvent's own precedent).
    if (!collapsed_) {
        if (Widget* content = ActiveContent()) {
            return content->OnEvent(event);
        }
    }
    return false;
}

} // namespace ned::ui
