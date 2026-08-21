#include "TabBar.h"

#include <algorithm>

#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    // U+00D7 MULTIPLICATION SIGN -- a conventional close-icon glyph, and
    // guaranteed single-column width in any monospace font, the same
    // portability reasoning already applied to every other glyph choice in
    // this codebase (ScrollArrowButton's arrows, ProjectSidebar's tree
    // connectors and disclosure triangles).
    constexpr char32_t kCloseIcon = U'×';
    constexpr char32_t kMoreLeft  = U'‹';
    constexpr char32_t kMoreRight = U'›';

    // U+258C LEFT HALF BLOCK (tab-restyle follow-up): the one-column end
    // cap after each tab's close icon. Drawn with the tab's own block
    // color as *foreground* on the row's buffer background, so the cell
    // reads as the tab's block tapering off at half a column -- Block
    // Elements are single-width in any monospace font, the same
    // portability bar every other chrome glyph here already clears
    // (Powerline-style rounded caps need a patched font, per
    // ProjectSidebar's own long-standing no-Nerd-Font note).
    constexpr char32_t kTabEndCap = U'▌';

    // 1-space padding, an asterisk if modified, a space, then the close
    // icon; one blank widget-background column as a separator before the
    // next tab (added by the caller, not here) keeps adjacent inactive tabs
    // from visually merging into one block. A u32string, not a std::string
    // byte-count, so its .size() is exactly the column width the close
    // icon's multi-byte UTF-8 encoding would otherwise throw off -- same
    // approach ProjectSidebar's own label-building already uses.
    std::u32string TabLabel(const text::Buffer& buffer) {
        std::u32string label = U" ";
        for (const char ch : buffer.Name()) {
            // Buffer names are treated as ASCII-ish here, same simplification
            // ModeLine's/ProjectSidebar's own name rendering already makes.
            label += static_cast<char32_t>(static_cast<unsigned char>(ch));
        }
        if (buffer.Modified()) {
            label += U'*';
        }
        label += U' ';
        label += kCloseIcon;
        return label;
    }

} // namespace

TabBar::TabBar(std::function<ActiveBuffer&()> activeBufferProvider, const text::BufferList& bufferList,
               const Theme& theme) : activeBufferProvider_(std::move(activeBufferProvider)), bufferList_(bufferList), theme_(theme) {
}

std::vector<TabBar::TabLayout> TabBar::ComputeTabLayout() const {
    std::vector<TabLayout> layout;
    int                    col = 0;
    for (const auto& buffer : bufferList_.Buffers()) {
        const int labelWidth = static_cast<int>(TabLabel(*buffer).size());
        // +1: the end-cap column after the close icon (tab-restyle
        // follow-up) -- part of the tab's own extent, so clicking it
        // switches like anywhere else on the tab; closeColumn stays the
        // label's final character (the × itself).
        const int width = labelWidth + 1;
        layout.push_back(TabLayout{
            .startColumn = col, .endColumn = col + width, .closeColumn = col + labelWidth - 1, .buffer = buffer.get()});
        // No separate gap column: the cap's own half-empty cell (buffer
        // background behind the ▌) is the visual separation between tabs.
        col += width;
    }
    return layout;
}

void TabBar::Paint(Canvas c) {
    // Tab-restyle follow-up: the row fills with the *buffer* background,
    // not the tab chrome color -- the 1-column gaps between tabs (and the
    // run after the last tab) show it through, so each tab reads as its
    // own distinct block instead of the whole strip merging into one bar
    // (the original fill made tabs visually indistinguishable, a real
    // user report).
    for (int x = 0; x < c.size().width; ++x) {
        Cell& cell            = c[{.x = x, .y = 0}];
        cell.character        = " ";
        cell.background_color = theme_.background;
    }

    const text::Buffer*          active  = &activeBufferProvider_().Get();
    const text::Buffer*          preview = bufferList_.PreviewBuffer();
    const std::vector<TabLayout> layout  = ComputeTabLayout();

    // Active-tab auto-reveal (tab-reveal follow-up): whenever the active
    // buffer has changed since the last Paint (a freshly opened file, a
    // switch-to-buffer, a tab click on a partially visible tab), scroll
    // just far enough that its whole tab is visible -- a newly opened
    // file's tab landing past the right edge of an overflowing row was
    // otherwise invisible, which read as the file not having opened at
    // all. Only on a change, so manually wheel-scrolling away from the
    // active tab isn't snapped back every frame. End-edge check first,
    // start-edge second, so a tab wider than the whole row shows its
    // start.
    if (active != lastRevealedActive_) {
        lastRevealedActive_ = active;
        for (const TabLayout& tab : layout) {
            if (tab.buffer == active) {
                if (tab.endColumn - scrollOffset_ > c.size().width) {
                    scrollOffset_ = tab.endColumn - c.size().width;
                }
                if (tab.startColumn < scrollOffset_) {
                    scrollOffset_ = tab.startColumn;
                }
                break;
            }
        }
    }
    // Clamped every frame, not just on wheel/reveal -- closing a tab can
    // shrink the row's total width out from under a large scrollOffset_.
    const int totalTabWidth = layout.empty() ? 0 : layout.back().endColumn;
    scrollOffset_           = std::clamp(scrollOffset_, 0, std::max(0, totalTabWidth - c.size().width));

    // Tab-restyle follow-up: with the underline row gone, the active tab
    // itself carries the "keyboard is in the editor" accent -- its block
    // takes the same accent-tinted chrome color the focused mode line's
    // gradient starts from, so the top and bottom edges of a focused pane
    // light up as one system (see SetFocusProvider).
    Brush activeBrush = theme_.activeTab;
    if (focusProvider_ && focusProvider_()) {
        activeBrush.background = theme_.modeLineFocusedGradientStart;
    }

    for (const TabLayout& tab : layout) {
        Brush brush = (tab.buffer == active) ? activeBrush : theme_.tabBar;
        if (tab.buffer == preview) {
            // Single-click-preview follow-up: italic marks a tab as
            // transient (VS Code's own convention for the same concept) --
            // no new Theme color needed, just a trait layered onto whatever
            // brush this tab would otherwise use.
            brush.italic = true;
        }
        const std::u32string label = TabLabel(*tab.buffer);

        for (std::size_t i = 0; i < label.size(); ++i) {
            const int col = tab.startColumn + static_cast<int>(i) - scrollOffset_;
            if (col < 0 || col >= c.size().width) {
                continue;
            }
            Cell& cell     = c[{.x = col, .y = 0}];
            cell.character = text::EncodeCodepointUtf8(label[i]);
            brush.ApplyTo(cell);
        }

        // The end cap -- see kTabEndCap. Assigned as a whole fresh Cell
        // (not just character + colors) so no trait leaks in from whatever
        // a previous frame left in this reused Screen cell.
        const int capCol = tab.startColumn + static_cast<int>(label.size()) - scrollOffset_;
        if (capCol >= 0 && capCol < c.size().width) {
            Cell& cell            = c[{.x = capCol, .y = 0}];
            cell                  = Cell{};
            cell.character        = text::EncodeCodepointUtf8(kTabEndCap);
            cell.foreground_color = brush.background;
            cell.background_color = theme_.background;
        }
    }

    // A `‹`/`›` overflow indicator at the corresponding edge whenever more
    // tab content has been scrolled past in that direction -- the mouse
    // wheel already scrolls this row, but nothing made that discoverable or
    // even visible that there was more to scroll to.
    const int totalWidth = layout.empty() ? 0 : layout.back().endColumn;
    if (c.size().width > 0) {
        if (scrollOffset_ > 0) {
            Cell& cell     = c[{.x = 0, .y = 0}];
            cell.character = text::EncodeCodepointUtf8(kMoreLeft);
            theme_.tabBar.ApplyTo(cell);
        }
        if (totalWidth - scrollOffset_ > c.size().width) {
            Cell& cell     = c[{.x = c.size().width - 1, .y = 0}];
            cell.character = text::EncodeCodepointUtf8(kMoreRight);
            theme_.tabBar.ApplyTo(cell);
        }
    }
}

bool TabBar::OnEvent(const Event& event) {
    const auto mouse = LocalMouseEvent(event);
    if (!mouse) {
        return false;
    }

    if (mouse->button == MouseEvent::Button::WheelDown || mouse->button == MouseEvent::Button::WheelUp) {
        constexpr int kScrollStep = 4;

        // No tilt-wheel distinction is available (see BufferView's own
        // WheelUp/WheelDown use for vertical scrolling) -- WheelDown reveals
        // later tabs, WheelUp reveals earlier ones, an arbitrary but
        // consistent mapping for a horizontal-only widget.
        if (mouse->button == MouseEvent::Button::WheelDown) {
            scrollOffset_ += kScrollStep;
        }
        else {
            scrollOffset_ -= kScrollStep;
        }

        const std::vector<TabLayout> layout     = ComputeTabLayout();
        const int                    totalWidth = layout.empty() ? 0 : layout.back().endColumn;
        const int                    maxScroll  = std::max(0, totalWidth - size().width);
        scrollOffset_                           = std::clamp(scrollOffset_, 0, maxScroll);
        return true;
    }

    // Tab-reorder follow-up: a left-press on a tab's body (below) starts a
    // potential drag; while it's held, Moved events reorder the dragged tab
    // to whichever tab index the pointer is over (live, VS Code-style --
    // the row re-lays-out as the drag progresses). Any release, or a Moved
    // without the left button still down (a release that happened outside
    // this row's bounds -- LocalMouseEvent never delivered it, same
    // no-mouse-capture reality every drag in this codebase handles), ends
    // the drag.
    if (dragBuffer_ != nullptr && mouse->motion == MouseEvent::Motion::Released) {
        dragBuffer_ = nullptr;
        return true;
    }
    if (dragBuffer_ != nullptr && mouse->motion == MouseEvent::Motion::Moved) {
        if (mouse->button != MouseEvent::Button::Left) {
            dragBuffer_ = nullptr;
            return false;
        }
        if (onReorder_) {
            const int                    column = mouse->at.x + scrollOffset_;
            const std::vector<TabLayout> layout = ComputeTabLayout();
            for (std::size_t i = 0; i < layout.size(); ++i) {
                const bool pastLastTab = (i + 1 == layout.size() && column >= layout[i].endColumn);
                if ((column >= layout[i].startColumn && column < layout[i].endColumn) || pastLastTab) {
                    if (layout[i].buffer != dragBuffer_) {
                        onReorder_(*dragBuffer_, i);
                    }
                    break;
                }
            }
        }
        return true;
    }

    if (mouse->button != MouseEvent::Button::Left || mouse->motion != MouseEvent::Motion::Pressed) {
        return false;
    }

    dragBuffer_             = nullptr;
    const int clickedColumn = mouse->at.x + scrollOffset_;
    for (const TabLayout& tab : ComputeTabLayout()) {
        if (clickedColumn == tab.closeColumn) {
            if (onCloseRequest_) {
                onCloseRequest_(*tab.buffer);
            }
            return true;
        }
        if (clickedColumn >= tab.startColumn && clickedColumn < tab.endColumn) {
            activeBufferProvider_().Set(*tab.buffer);
            dragBuffer_ = tab.buffer;
            return true;
        }
    }
    return true;
}

void TabBar::SetOnReorder(std::function<void(text::Buffer&, std::size_t)> handler) {
    onReorder_ = std::move(handler);
}

void TabBar::SetOnCloseRequest(std::function<void(text::Buffer&)> handler) {
    onCloseRequest_ = std::move(handler);
}

void TabBar::SetFocusProvider(std::function<bool()> provider) {
    focusProvider_ = std::move(provider);
}

} // namespace ned::ui
