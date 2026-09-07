#include "PanelDock.h"

#include <algorithm>

#include "Border.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    constexpr char32_t kMaximizeIcon = U'▲';
    constexpr char32_t kCloseIcon    = U'×';

    // Multiple-terminal-tabs follow-up: TabBar's own overflow-indicator
    // glyphs, reused verbatim for the same "the wheel scrolls this row but
    // nothing said so" problem, now that this row can overflow too (more
    // terminal tabs than fit).
    constexpr char32_t kMoreLeft  = U'‹';
    constexpr char32_t kMoreRight = U'›';

    // Column width of one bracketed [x]/[▲]/action-icon button, including
    // the one-space gap before it -- TerminalPanel's own button layout,
    // generalized so this file lays out however many buttons a tab
    // contributes without hardcoding per-button offsets.
    constexpr int kButtonWidth = 4; // " [x]"

    // One codepoint per column, PaintUtf8Row's own convention -- used here
    // just to measure a tab label's painted width for hit-testing, since a
    // dynamic titleText (an agent name, say) isn't guaranteed ASCII the way
    // the fixed "Terminal"/"Debug console" labels are.
    int ColumnCount(std::string_view text) {
        int         columns = 0;
        std::size_t pos     = 0;
        while (pos < text.size()) {
            pos = text::NextCodepointBoundary(text, pos);
            ++columns;
        }
        return columns;
    }

} // namespace

PanelDock::PanelDock(const Theme& theme) : theme_(theme) {
}

PanelDock::Entry* PanelDock::FindEntry(std::size_t id) {
    for (Entry& entry : entries_) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

const PanelDock::Entry* PanelDock::FindEntry(std::size_t id) const {
    for (const Entry& entry : entries_) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

std::size_t PanelDock::AddPanel(std::string label, Widget& panel, std::function<std::string()> titleText,
                                std::function<int()> getPercent, std::function<void(int)> setPercent,
                                std::function<std::vector<TabAction>()> extraActions) {
    const std::size_t id = nextId_++;
    entries_.push_back(Entry{.id           = id,
                             .label        = std::move(label),
                             .panel        = &panel,
                             .titleText    = std::move(titleText),
                             .getPercent   = std::move(getPercent),
                             .setPercent   = std::move(setPercent),
                             .extraActions = std::move(extraActions)});
    if (entries_.size() == 1) {
        // The first tab ever registered (or the first one registered again
        // after every other tab was removed) becomes active by default --
        // matching this class's original always-active-tab-0 behavior
        // without assuming a freshly issued id happens to be 0.
        active_ = id;
    }
    return id;
}

void PanelDock::RemovePanel(std::size_t id) {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [id](const Entry& entry) { return entry.id == id; });
    if (it == entries_.end()) {
        return;
    }
    const std::size_t pos       = static_cast<std::size_t>(it - entries_.begin());
    const bool        wasActive = (id == active_);
    entries_.erase(it);

    if (!wasActive) {
        return; // active tab's own identity and position are untouched
    }
    if (entries_.empty()) {
        if (onLayoutChange_) {
            onLayoutChange_();
        }
        return;
    }
    // The neighboring tab at the same screen position takes over (closing a
    // browser tab's own convention), falling back to the new last tab if
    // the removed one was rightmost.
    const std::size_t newPos = std::min(pos, entries_.size() - 1);
    active_                  = entries_[newPos].id;
    RepositionActivePanel();
    if (Widget* panel = ActivePanel()) {
        panel->TakeFocus();
    }
    RevealActiveTab();
    if (onLayoutChange_) {
        onLayoutChange_();
    }
}

std::optional<int> PanelDock::ActivePercent() const {
    const Entry* entry = FindEntry(active_);
    if (entry == nullptr || !entry->getPercent) {
        return std::nullopt;
    }
    return entry->getPercent();
}

Widget* PanelDock::ActivePanel() const {
    const Entry* entry = FindEntry(active_);
    return entry != nullptr ? entry->panel : nullptr;
}

std::vector<PanelDock::TabLabelSpan> PanelDock::ComputeTabLabelLayout() const {
    std::vector<TabLabelSpan> layout;
    int                       col = 0; // content-space: column 0 is the strip's 1-column left margin
    for (const Entry& entry : entries_) {
        const std::string text  = entry.titleText ? entry.titleText() : entry.label;
        const std::string label = (entry.id == active_) ? ("[ " + text + " ]") : (" " + text + " ");
        const int         width = ColumnCount(label);
        layout.push_back(TabLabelSpan{.id = entry.id, .startColumn = col, .endColumn = col + width});
        col += width + 1; // +1: gap before the next tab
    }
    return layout;
}

int PanelDock::ButtonClusterStartColumn() const {
    const int    width       = size().width;
    std::size_t  buttonCount = 2; // maximize + close, always present
    const Entry* entry       = FindEntry(active_);
    if (entry != nullptr && entry->extraActions) {
        buttonCount += entry->extraActions().size();
    }
    const int totalButtonsWidth = static_cast<int>(buttonCount) * kButtonWidth;
    return width >= totalButtonsWidth ? width - totalButtonsWidth : width;
}

void PanelDock::RevealActiveTab() {
    const std::vector<TabLabelSpan> layout         = ComputeTabLabelLayout();
    const int                       labelViewWidth = std::max(0, ButtonClusterStartColumn() - 1);
    for (const TabLabelSpan& span : layout) {
        if (span.id != active_) {
            continue;
        }
        if (span.endColumn - tabScrollOffset_ > labelViewWidth) {
            tabScrollOffset_ = span.endColumn - labelViewWidth;
        }
        if (span.startColumn < tabScrollOffset_) {
            tabScrollOffset_ = span.startColumn;
        }
        break;
    }
    const int totalLabelWidth = layout.empty() ? 0 : layout.back().endColumn;
    tabScrollOffset_          = std::clamp(tabScrollOffset_, 0, std::max(0, totalLabelWidth - labelViewWidth));
}

void PanelDock::SwitchTo(std::size_t id) {
    if (FindEntry(id) == nullptr) {
        return;
    }
    const bool changed = id != active_;
    active_            = id;
    RepositionActivePanel();
    if (Widget* panel = ActivePanel()) {
        panel->TakeFocus();
    }
    if (changed) {
        RevealActiveTab();
    }
    // A different tab can resolve a different ActivePercent() -- without
    // this, the dock's own outer Box_ (only ever recomputed by OverlayHost
    // on Show()/Reflow(), never per-event) would keep whichever height the
    // *previously* active tab had until the next real terminal resize.
    // Confirmed live over a real pty: switching from Terminal to Debug
    // console left the dock at Terminal's own configured height instead of
    // shrinking to Debug console's, until this fix.
    if (changed && onLayoutChange_) {
        onLayoutChange_();
    }
}

void PanelDock::SetOnLayoutChange(std::function<void()> onLayoutChange) {
    onLayoutChange_ = std::move(onLayoutChange);
}

void PanelDock::SetOnCloseRequest(std::function<void()> onClose) {
    onCloseRequest_ = std::move(onClose);
}

void PanelDock::SetTerminalSize(Size size) {
    terminalSize_ = size;
}

void PanelDock::RepositionActivePanel() {
    Widget* panel = ActivePanel();
    if (panel == nullptr) {
        return;
    }
    const Box& box = Box_();
    panel->SetBox_(Box{.x_min = box.x_min, .x_max = box.x_max, .y_min = box.y_min + 1, .y_max = box.y_max});
}

void PanelDock::OnResize(Size /*previous*/) {
    RepositionActivePanel();
}

void PanelDock::Paint(Canvas canvas) {
    const int width  = canvas.size().width;
    const int height = canvas.size().height;
    if (width <= 0 || height <= 0 || entries_.empty()) {
        return;
    }
    // Idempotent and cheap -- guards against AddPanel being called after
    // this dock's own Box_ was already set (SwitchTo/OnResize otherwise
    // being the only triggers), which would otherwise leave the
    // default-active first panel's Box_ at its stale/zero default forever.
    RepositionActivePanel();

    Widget*           active     = ActivePanel();
    const Brush&      frameBrush = (active != nullptr && active->Focused()) ? theme_.borderAccent : theme_.border;
    const std::string horizontal = text::EncodeCodepointUtf8(RoundedBorderGlyphs().horizontal);
    for (int x = 0; x < width; ++x) {
        Cell& cell     = canvas[{.x = x, .y = 0}];
        cell.character = horizontal;
        frameBrush.ApplyTo(cell);
    }

    // Multiple-terminal-tabs follow-up: the label region is scrollable, so
    // its own writable band ends at ButtonClusterStartColumn() rather than
    // the canvas's own right edge -- everything from there on is the
    // right-aligned button cluster below.
    const int                       buttonClusterStart = ButtonClusterStartColumn();
    const int                       labelViewWidth     = std::max(0, buttonClusterStart - 1);
    const std::vector<TabLabelSpan> layout             = ComputeTabLabelLayout();
    const int                       totalLabelWidth    = layout.empty() ? 0 : layout.back().endColumn;
    tabScrollOffset_                                   = std::clamp(tabScrollOffset_, 0, std::max(0, totalLabelWidth - labelViewWidth));

    for (const TabLabelSpan& span : layout) {
        const Entry* entry = FindEntry(span.id);
        if (entry == nullptr) {
            continue; // shouldn't happen -- layout is derived from entries_ itself
        }
        const std::string text    = entry->titleText ? entry->titleText() : entry->label;
        const std::string label   = (span.id == active_) ? ("[ " + text + " ]") : (" " + text + " ");
        const int         screenX = 1 + span.startColumn - tabScrollOffset_;
        const int         maxCols = buttonClusterStart - screenX;
        if (maxCols > 0) {
            PaintUtf8Row(canvas, screenX, 0, label, frameBrush, maxCols);
        }
    }

    // A `‹`/`›` overflow indicator at the corresponding edge whenever more
    // tab content has been scrolled past in that direction, mirroring
    // TabBar's own indicator for the buffer tab strip.
    if (labelViewWidth > 0) {
        if (tabScrollOffset_ > 0) {
            Cell& cell     = canvas[{.x = 0, .y = 0}];
            cell.character = text::EncodeCodepointUtf8(kMoreLeft);
            frameBrush.ApplyTo(cell);
        }
        if (totalLabelWidth - tabScrollOffset_ > labelViewWidth) {
            Cell& cell     = canvas[{.x = buttonClusterStart - 1, .y = 0}];
            cell.character = text::EncodeCodepointUtf8(kMoreRight);
            frameBrush.ApplyTo(cell);
        }
    }

    // Right-aligned chrome: the active tab's own extra actions, then the
    // shared maximize/close -- TerminalPanel's own right-aligned button-row
    // convention, generalized to however many buttons apply this frame.
    std::vector<std::pair<char32_t, std::function<void()>>> rightButtons;
    if (const Entry* entry = FindEntry(active_); entry != nullptr && entry->extraActions) {
        for (const TabAction& action : entry->extraActions()) {
            rightButtons.emplace_back(action.icon, action.onClick);
        }
    }
    rightButtons.emplace_back(kMaximizeIcon, nullptr);
    rightButtons.emplace_back(kCloseIcon, nullptr);

    const int totalButtonsWidth = static_cast<int>(rightButtons.size()) * kButtonWidth;
    if (width >= totalButtonsWidth) {
        int bx = width - totalButtonsWidth;
        for (const auto& [icon, unused] : rightButtons) {
            (void)unused;
            const std::string glyphs[3] = {"[", text::EncodeCodepointUtf8(icon), "]"};
            for (int i = 0; i < 3; ++i) {
                Cell& cell     = canvas[{.x = bx + 1 + i, .y = 0}];
                cell.character = glyphs[i];
                frameBrush.ApplyTo(cell);
            }
            bx += kButtonWidth;
        }
    }

    if (height < 2 || active == nullptr) {
        return;
    }
    active->Paint(canvas.ForBox(active->Box_()));
}

bool PanelDock::HandleTabStripClick(int x) {
    const int width = size().width;

    // Right-aligned chrome first -- same set/order Paint just drew.
    std::vector<std::function<void()>> rightButtons;
    if (const Entry* entry = FindEntry(active_); entry != nullptr && entry->extraActions) {
        for (const TabAction& action : entry->extraActions()) {
            rightButtons.push_back(action.onClick);
        }
    }
    rightButtons.push_back([this] {
        maximized_ = !maximized_;
        if (onLayoutChange_) {
            onLayoutChange_();
        }
    });
    rightButtons.push_back([this] {
        if (onCloseRequest_) {
            onCloseRequest_();
        }
    });

    const int totalButtonsWidth = static_cast<int>(rightButtons.size()) * kButtonWidth;
    if (width >= totalButtonsWidth) {
        const int firstButtonX = width - totalButtonsWidth;
        if (x >= firstButtonX) {
            const int index = std::min((x - firstButtonX) / kButtonWidth, static_cast<int>(rightButtons.size()) - 1);
            if (rightButtons[static_cast<std::size_t>(index)]) {
                rightButtons[static_cast<std::size_t>(index)]();
            }
            return true;
        }
    }

    // Tab labels -- convert the click's screen column into content-space
    // (undoing the scroll offset the same way ComputeTabLabelLayout leaves
    // it) before comparing against each span, so a click always lands on
    // whichever tab it visually looks like it did.
    if (x >= 1) {
        const int                       contentColumn = x - 1 + tabScrollOffset_;
        const std::vector<TabLabelSpan> layout        = ComputeTabLabelLayout();
        for (const TabLabelSpan& span : layout) {
            if (contentColumn >= span.startColumn && contentColumn < span.endColumn) {
                SwitchTo(span.id);
                return true;
            }
        }
    }

    return false; // unclaimed row-0 column -- start a resize-drag instead
}

void PanelDock::BeginResize(Point globalMouse) {
    resizing_           = true;
    resizeAnchorGlobal_ = globalMouse;
    const Entry* entry  = FindEntry(active_);
    resizeStartPercent_ = (entry != nullptr && entry->getPercent) ? entry->getPercent() : 0;
}

void PanelDock::UpdateResize(Point globalMouse) {
    const Entry* entry = FindEntry(active_);
    if (entry == nullptr || !entry->setPercent) {
        return;
    }
    if (terminalSize_.height <= 0) {
        return; // SetTerminalSize never called yet -- see its own doc comment
    }
    // Dragging the tab strip upward (away from the dock's own content)
    // grows it -- TerminalPanel/AcpPanel's own bottom-dock convention,
    // "anchor minus current" so a move in the growing direction is positive.
    const int deltaPixels  = resizeAnchorGlobal_.y - globalMouse.y;
    const int deltaPercent = deltaPixels * 100 / terminalSize_.height;
    entry->setPercent(resizeStartPercent_ + deltaPercent);
}

void PanelDock::EndResize() {
    resizing_ = false;
}

bool PanelDock::OnEvent(const Event& event) {
    if (!event.is_mouse()) {
        return false; // keyboard never reaches here -- see this file's header comment
    }
    const MouseEvent rawMouse = event.mouse();

    // ProjectSidebar/AcpPanel's own resize-drag shape: Moved/Released are
    // handled against the *global* mouse position before the local-bounds
    // hit test below, since a fast drag can carry the cursor outside this
    // widget's own Box mid-session.
    if (rawMouse.motion == MouseEvent::Motion::Moved && resizing_) {
        UpdateResize(rawMouse.at);
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

    if (mouse->at.y == 0 && (mouse->button == MouseEvent::Button::WheelUp || mouse->button == MouseEvent::Button::WheelDown)) {
        // Multiple-terminal-tabs follow-up: TabBar's own wheel-scroll
        // convention for the buffer tab strip, applied to this dock's tab
        // strip now that it can overflow too.
        constexpr int kScrollStep = 4;
        tabScrollOffset_ += (mouse->button == MouseEvent::Button::WheelDown) ? kScrollStep : -kScrollStep;

        const std::vector<TabLabelSpan> layout          = ComputeTabLabelLayout();
        const int                       totalLabelWidth = layout.empty() ? 0 : layout.back().endColumn;
        const int                       labelViewWidth  = std::max(0, ButtonClusterStartColumn() - 1);
        tabScrollOffset_                                = std::clamp(tabScrollOffset_, 0, std::max(0, totalLabelWidth - labelViewWidth));
        return true;
    }

    if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
        if (mouse->at.y == 0) {
            if (!HandleTabStripClick(mouse->at.x)) {
                BeginResize(rawMouse.at);
            }
            return true;
        }
    }

    // Anything else -- including every non-row-0 mouse event -- forwards
    // unmodified (still global coordinates) to the active panel's own
    // OnEvent, which does its own LocalMouseEvent translation against its
    // own Box_() exactly as it does standalone.
    if (Widget* active = ActivePanel()) {
        return active->OnEvent(event);
    }
    return false;
}

} // namespace ned::ui
