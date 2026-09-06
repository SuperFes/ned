#include "PanelDock.h"

#include <algorithm>

#include "Border.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    constexpr char32_t kMaximizeIcon = U'▲';
    constexpr char32_t kCloseIcon    = U'×';

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

std::size_t PanelDock::AddPanel(std::string label, Widget& panel, std::function<std::string()> titleText,
                                std::function<int()> getPercent, std::function<void(int)> setPercent,
                                std::function<std::vector<TabAction>()> extraActions) {
    entries_.push_back(Entry{.label        = std::move(label),
                             .panel        = &panel,
                             .titleText    = std::move(titleText),
                             .getPercent   = std::move(getPercent),
                             .setPercent   = std::move(setPercent),
                             .extraActions = std::move(extraActions)});
    return entries_.size() - 1;
}

std::optional<int> PanelDock::ActivePercent() const {
    if (active_ >= entries_.size() || !entries_[active_].getPercent) {
        return std::nullopt;
    }
    return entries_[active_].getPercent();
}

Widget* PanelDock::ActivePanel() const {
    if (active_ >= entries_.size()) {
        return nullptr;
    }
    return entries_[active_].panel;
}

void PanelDock::SwitchTo(std::size_t index) {
    if (entries_.empty()) {
        return;
    }
    const std::size_t clamped = std::min(index, entries_.size() - 1);
    const bool        changed = clamped != active_;
    active_                   = clamped;
    RepositionActivePanel();
    if (Widget* panel = ActivePanel()) {
        panel->TakeFocus();
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

    // Tab labels, left to right -- the active one bracketed, the rest plain.
    int x = 1;
    for (std::size_t i = 0; i < entries_.size() && x < width; ++i) {
        const Entry&      entry = entries_[i];
        const std::string text  = entry.titleText ? entry.titleText() : entry.label;
        const std::string label = (i == active_) ? ("[ " + text + " ]") : (" " + text + " ");
        x += PaintUtf8Row(canvas, x, 0, label, frameBrush, width - x) + 1; // +1: gap before the next tab
    }

    // Right-aligned chrome: the active tab's own extra actions, then the
    // shared maximize/close -- TerminalPanel's own right-aligned button-row
    // convention, generalized to however many buttons apply this frame.
    std::vector<std::pair<char32_t, std::function<void()>>> rightButtons;
    if (active_ < entries_.size() && entries_[active_].extraActions) {
        for (const TabAction& action : entries_[active_].extraActions()) {
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
    if (active_ < entries_.size() && entries_[active_].extraActions) {
        for (const TabAction& action : entries_[active_].extraActions()) {
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

    // Tab labels -- same left-to-right walk Paint uses, so a click always
    // lands on whichever tab it visually looks like it did.
    int tx = 1;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry&      entry      = entries_[i];
        const std::string text       = entry.titleText ? entry.titleText() : entry.label;
        const std::string label      = (i == active_) ? ("[ " + text + " ]") : (" " + text + " ");
        const int         labelWidth = ColumnCount(label);
        if (x >= tx && x < tx + labelWidth) {
            SwitchTo(i);
            return true;
        }
        tx += labelWidth + 1;
    }

    return false; // unclaimed row-0 column -- start a resize-drag instead
}

void PanelDock::BeginResize(Point globalMouse) {
    resizing_           = true;
    resizeAnchorGlobal_ = globalMouse;
    resizeStartPercent_ = (active_ < entries_.size() && entries_[active_].getPercent) ? entries_[active_].getPercent() : 0;
}

void PanelDock::UpdateResize(Point globalMouse) {
    if (active_ >= entries_.size() || !entries_[active_].setPercent) {
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
    entries_[active_].setPercent(resizeStartPercent_ + deltaPercent);
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
