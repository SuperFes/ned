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
        const int width = static_cast<int>(TabLabel(*buffer).size());
        layout.push_back(TabLayout{
            .startColumn = col, .endColumn = col + width, .closeColumn = col + width - 1, .buffer = buffer.get()});
        col += width + 1; // 1-column gap between tabs
    }
    return layout;
}

void TabBar::Paint(Canvas c) {
    for (int x = 0; x < c.size().width; ++x) {
        ftxui::Cell& cell     = c[{.x = x, .y = 0}];
        cell.character        = " ";
        cell.background_color = theme_.tabBar.background.ToFtxui();
    }

    const text::Buffer*          active  = &activeBufferProvider_().Get();
    const text::Buffer*          preview = bufferList_.PreviewBuffer();
    const std::vector<TabLayout> layout  = ComputeTabLayout();

    for (const TabLayout& tab : layout) {
        Brush brush = (tab.buffer == active) ? theme_.activeTab : theme_.tabBar;
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
            ftxui::Cell& cell = c[{.x = col, .y = 0}];
            cell.character    = text::EncodeCodepointUtf8(label[i]);
            brush.ApplyTo(cell);
        }
    }

    // A `‹`/`›` overflow indicator at the corresponding edge whenever more
    // tab content has been scrolled past in that direction -- the mouse
    // wheel already scrolls this row, but nothing made that discoverable or
    // even visible that there was more to scroll to.
    const int totalWidth = layout.empty() ? 0 : layout.back().endColumn;
    if (c.size().width > 0) {
        if (scrollOffset_ > 0) {
            ftxui::Cell& cell = c[{.x = 0, .y = 0}];
            cell.character    = text::EncodeCodepointUtf8(kMoreLeft);
            theme_.tabBar.ApplyTo(cell);
        }
        if (totalWidth - scrollOffset_ > c.size().width) {
            ftxui::Cell& cell = c[{.x = c.size().width - 1, .y = 0}];
            cell.character    = text::EncodeCodepointUtf8(kMoreRight);
            theme_.tabBar.ApplyTo(cell);
        }
    }
}

bool TabBar::OnEvent(ftxui::Event event) {
    const auto mouse = LocalMouseEvent(event);
    if (!mouse) {
        return false;
    }

    if (mouse->button == ftxui::Mouse::WheelDown || mouse->button == ftxui::Mouse::WheelUp) {
        constexpr int kScrollStep = 4;

        // No tilt-wheel distinction is available (see BufferView's own
        // WheelUp/WheelDown use for vertical scrolling) -- WheelDown reveals
        // later tabs, WheelUp reveals earlier ones, an arbitrary but
        // consistent mapping for a horizontal-only widget.
        if (mouse->button == ftxui::Mouse::WheelDown) {
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

    if (mouse->button != ftxui::Mouse::Left || mouse->motion != ftxui::Mouse::Pressed) {
        return false;
    }

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
            return true;
        }
    }
    return true;
}

void TabBar::SetOnCloseRequest(std::function<void(text::Buffer&)> handler) {
    onCloseRequest_ = std::move(handler);
}

} // namespace ned::ui
