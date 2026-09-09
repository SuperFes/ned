#include "PanelDock.h"

#include <algorithm>

#include "Border.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    // tab-glyph-redesign follow-up: maximize/restore now actually toggles
    // (a real bug -- the button used to always paint kMaximizeIcon even
    // once already maximized, since nothing ever consulted Maximized()
    // when choosing which glyph to draw).
    constexpr char32_t kMaximizeIcon = U'▲';
    constexpr char32_t kRestoreIcon  = U'▼';

    // The shared "hide the whole dock" button's own glyph -- deliberately
    // NOT kCloseIcon below. A per-tab action (Terminal's own close-this-tab
    // TabAction, main.cpp) already uses kCloseIcon for "close this one tab";
    // reusing it here for "hide the entire dock" put the same × in two
    // places doing two different things, a real reported confusion
    // ("one of the x's for the terminal just closes the terminal and not
    // the bottom tab bar, but it should not be the same glyph in both
    // places"). ▾ reads as "collapse/tuck away" rather than "destroy."
    constexpr char32_t kHideDockIcon = U'▾';

    // A per-tab action's own close glyph (Terminal's "close this tab"
    // TabAction) -- distinct from kHideDockIcon above.
    constexpr char32_t kCloseIcon = U'×';

    // Multiple-terminal-tabs follow-up: TabBar's own overflow-indicator
    // glyphs, reused verbatim for the same "the wheel scrolls this row but
    // nothing said so" problem, now that this row can overflow too (more
    // terminal tabs than fit).
    constexpr char32_t kMoreLeft  = U'‹';
    constexpr char32_t kMoreRight = U'›';

    // tab-glyph-redesign follow-up: TabBar.cpp's own end-cap separator,
    // reused verbatim -- a half-filled block whose foreground matches the
    // tab's own background and whose background matches the strip's base
    // background, giving each tab a soft rounded-looking right edge instead
    // of a hard color cut. Replaces the bracket-based "[ Name ]" active-tab
    // convention this file used to share with the button cluster below (the
    // literal cause of the "same [ ] chars used for two different parts of
    // the tabbed interface" complaint).
    constexpr char32_t kTabEndCap = U'▌'; // LEFT HALF BLOCK

    // Column width of one action/maximize/hide button, including the
    // one-space gap before it -- TerminalPanel's own button layout,
    // generalized so this file lays out however many buttons a tab
    // contributes without hardcoding per-button offsets. No longer
    // bracketed (tab-glyph-redesign follow-up) -- the 4 columns are now
    // [gap][pad][icon][pad], not [gap]['[']icon[']'].
    constexpr int kButtonWidth = 4; // " " + " x "

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

void PanelDock::EnsureSharedPercentInitialized() const {
    if (sharedPercentInitialized_) {
        return;
    }
    for (const Entry& entry : entries_) {
        if (entry.getPercent) {
            sharedPercent_ = entry.getPercent();
            break;
        }
    }
    sharedPercentInitialized_ = true;
}

int PanelDock::Percent() const {
    EnsureSharedPercentInitialized();
    return sharedPercent_;
}

Widget* PanelDock::ActivePanel() const {
    const Entry* entry = FindEntry(active_);
    return entry != nullptr ? entry->panel : nullptr;
}

std::vector<PanelDock::TabLabelSpan> PanelDock::ComputeTabLabelLayout() const {
    std::vector<TabLabelSpan> layout;
    int                       col = 0; // content-space: column 0 is the strip's 1-column left margin
    for (const Entry& entry : entries_) {
        // tab-glyph-redesign follow-up: same padded label text regardless
        // of active/inactive (TabBar.cpp's own convention) -- which tab is
        // active is now a color distinction (theme_.activeTab vs.
        // theme_.tabBar in Paint), not a bracket the label text itself
        // grows/shrinks by, so span width no longer needs to differ either.
        const std::string text  = entry.titleText ? entry.titleText() : entry.label;
        const std::string label = " " + text + " ";
        const int         width = ColumnCount(label) + 1; // +1: the end-cap column, TabBar's own layout
        layout.push_back(TabLabelSpan{.id = entry.id, .startColumn = col, .endColumn = col + width});
        col += width; // no separate gap column -- the cap's own half-empty cell is the visual separation
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
    // tab-height-unification follow-up: switching tabs no longer changes
    // this dock's own height (Percent() is one value shared by every tab
    // now, not read fresh per-tab), so there's nothing here for
    // onLayoutChange_ to react to -- the outer Box_ stays exactly where it
    // was across a switch. This used to call onLayoutChange_ on every
    // switch specifically because a different tab could resolve a
    // different ActivePercent(); that's the real, reported "grouped tabs,
    // but the dock still jumps around" behavior this whole follow-up
    // removes.
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
        // tab-glyph-redesign follow-up: TabBar.cpp's own colored-block tab
        // convention -- which tab is active is a brush distinction, not a
        // bracket in the text.
        const std::string text      = entry->titleText ? entry->titleText() : entry->label;
        const std::string label     = " " + text + " ";
        const Brush&      brush     = (span.id == active_) ? theme_.activeTab : theme_.tabBar;
        const int         screenX   = 1 + span.startColumn - tabScrollOffset_;
        const int         labelCols = span.endColumn - span.startColumn - 1; // -1: the end-cap column
        const int         maxCols   = std::min(labelCols, buttonClusterStart - screenX);
        if (maxCols > 0) {
            PaintUtf8Row(canvas, screenX, 0, label, brush, maxCols);
        }
        // The end-cap column itself -- TabBar.cpp's own half-block
        // separator, foreground matching this tab's own background so it
        // reads as a soft rounded right edge rather than a hard color cut.
        const int capX = screenX + labelCols;
        if (capX >= 0 && capX < buttonClusterStart && capX < width) {
            Cell& cell            = canvas[{.x = capX, .y = 0}];
            cell                  = Cell{};
            cell.character        = text::EncodeCodepointUtf8(kTabEndCap);
            cell.foreground_color = brush.background;
            cell.background_color = theme_.background;
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
    // tab-glyph-redesign follow-up: maximize toggles between kMaximizeIcon/
    // kRestoreIcon based on actual Maximized() state (previously always
    // painted kMaximizeIcon regardless -- a real bug, never caught because
    // nothing compared against Maximized() at paint time). The shared hide-
    // dock button uses kHideDockIcon, not kCloseIcon -- see that constant's
    // own comment on why it must differ from a per-tab close action.
    rightButtons.emplace_back(maximized_ ? kRestoreIcon : kMaximizeIcon, nullptr);
    rightButtons.emplace_back(kHideDockIcon, nullptr);

    const int totalButtonsWidth = static_cast<int>(rightButtons.size()) * kButtonWidth;
    if (width >= totalButtonsWidth) {
        int bx = width - totalButtonsWidth;
        for (const auto& [icon, unused] : rightButtons) {
            (void)unused;
            // No brackets (tab-glyph-redesign follow-up) -- just the icon,
            // padded, so it doesn't share the `[ ]` convention the tab
            // labels used to use for "active" (the literal "same chars used
            // for two different parts of the interface" complaint).
            const std::string glyphs[3] = {" ", text::EncodeCodepointUtf8(icon), " "};
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
    // tab-height-unification follow-up: anchors on the dock's own shared
    // height, not whichever percent the active tab happens to own -- a drag
    // started while, say, Debug console (no getPercent at all) is active
    // must still anchor on the real current height, not 0.
    EnsureSharedPercentInitialized();
    resizeStartPercent_ = sharedPercent_;
}

void PanelDock::UpdateResize(Point globalMouse) {
    if (terminalSize_.height <= 0) {
        return; // SetTerminalSize never called yet -- see its own doc comment
    }
    // Dragging the tab strip upward (away from the dock's own content)
    // grows it -- TerminalPanel/AcpPanel's own bottom-dock convention,
    // "anchor minus current" so a move in the growing direction is positive.
    const int deltaPixels  = resizeAnchorGlobal_.y - globalMouse.y;
    const int deltaPercent = deltaPixels * 100 / terminalSize_.height;
    const int newPercent   = resizeStartPercent_ + deltaPercent;

    // tab-height-unification follow-up: the drag still writes through
    // whichever (get, set)-percent pair the *active* tab registered, so
    // every existing ned/set-*-percent binding keeps persisting exactly as
    // before -- but the shared height applies from here on regardless of
    // which tab is later switched to, not just this one. A tab with no
    // setPercent at all (DebugConsole/Janet REPL) still resizes the dock
    // live for this drag, just with nothing to persist.
    const Entry* entry = FindEntry(active_);
    if (entry != nullptr && entry->setPercent) {
        entry->setPercent(newPercent);
        sharedPercent_ = entry->getPercent ? entry->getPercent() : newPercent;
    }
    else {
        sharedPercent_ = std::clamp(newPercent, 10, 90);
    }
    sharedPercentInitialized_ = true;
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
