#include "VcsDiffPreview.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "Border.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    constexpr int kHeaderHeight       = 1;
    constexpr int kBottomBorderHeight = 1;

    std::vector<std::string> SplitLines(const std::string& text) {
        std::vector<std::string> lines;
        std::istringstream       stream(text);
        std::string              line;
        while (std::getline(stream, line)) {
            lines.push_back(line);
        }
        return lines;
    }

    // Filenames/diff text are treated as ASCII-ish here, the same
    // simplification ProjectSidebar/VcsPanel's own ToCodepoints already make.
    std::u32string ToCodepoints(const std::string& text) {
        std::u32string out;
        for (const char ch : text) {
            out += static_cast<char32_t>(static_cast<unsigned char>(ch));
        }
        return out;
    }

} // namespace

VcsDiffPreview::VcsDiffPreview(const Theme& theme) : theme_(theme) {
}

void VcsDiffPreview::SetModel(std::optional<VcsDiffPreviewModel> model) {
    model_ = std::move(model);
    scrollOffset_ = 0; // a new file's diff always starts scrolled to the top
}

void VcsDiffPreview::SetOnHunkStageToggle(std::function<void(const std::filesystem::path&, std::size_t, bool)> handler) {
    onHunkStageToggle_ = std::move(handler);
}

int VcsDiffPreview::AffordanceWidth() const {
    if (!model_) {
        return 0;
    }
    return model_->staged ? 10 : 8; // "[unstage] " vs "[stage] "
}

std::vector<VcsDiffPreview::Row> VcsDiffPreview::BuildRows() const {
    std::vector<Row> rows;
    if (!model_) {
        return rows;
    }
    for (std::size_t i = 0; i < model_->hunks.size(); ++i) {
        const editor::vcs::DiffHunkText& hunk = model_->hunks[i];
        rows.push_back(Row{.isHeader = true, .hunkIndex = i, .text = hunk.hunkHeader});
        for (const std::string& line : SplitLines(hunk.bodyText)) {
            rows.push_back(Row{.isHeader = false, .hunkIndex = i, .text = line});
        }
    }
    return rows;
}

void VcsDiffPreview::Paint(Canvas c) {
    const Brush blankBrush{.background = theme_.background, .foreground = theme_.defaultForeground};
    for (int row = 0; row < c.size().height; ++row) {
        for (int col = 0; col < c.size().width; ++col) {
            Cell& cell     = c[{.x = col, .y = row}];
            cell.character = " ";
            blankBrush.ApplyTo(cell);
        }
    }

    DrawBorder(c, theme_.border);

    std::string title = "Diff preview";
    if (model_) {
        title += ": " + model_->path.filename().string() + (model_->staged ? " (staged)" : " (unstaged)");
    }
    DrawBorderTitle(c, title, theme_.borderAccent);

    if (!model_) {
        return;
    }

    const int contentLeft    = 1;
    const int contentColumns = std::max(0, c.size().width - 2);
    const int contentHeight  = std::max(0, c.size().height - kHeaderHeight - kBottomBorderHeight);

    const std::vector<Row> rows = BuildRows();
    for (int contentRow = 0; contentRow < contentHeight; ++contentRow) {
        const std::size_t index = static_cast<std::size_t>(scrollOffset_ + contentRow);
        if (index >= rows.size()) {
            break;
        }
        const Row& row = rows[index];
        const int  y   = contentRow + kHeaderHeight;

        std::u32string label;
        Brush           brush{.background = theme_.background, .foreground = theme_.defaultForeground};

        if (row.isHeader) {
            label = ToCodepoints(model_->staged ? "[unstage] " : "[stage] ");
            label += ToCodepoints(row.text);
            brush.foreground = theme_.borderAccent.foreground;
        }
        else {
            label = ToCodepoints(row.text);
            if (row.text.starts_with('+')) {
                brush.foreground = Color::BrightGreen;
            }
            else if (row.text.starts_with('-')) {
                brush.foreground = Color::BrightRed;
            }
            else {
                brush.foreground = theme_.lineNumberForeground;
            }
        }

        for (std::size_t i = 0; i < label.size() && static_cast<int>(i) < contentColumns; ++i) {
            Cell& cell     = c[{.x = contentLeft + static_cast<int>(i), .y = y}];
            cell.character = text::EncodeCodepointUtf8(label[i]);
            brush.ApplyTo(cell);
        }
    }
}

bool VcsDiffPreview::OnEvent(const Event& event) {
    if (!event.is_mouse()) {
        return false;
    }
    const auto mouse = LocalMouseEvent(event);
    if (!mouse) {
        return false;
    }

    if (mouse->button == MouseEvent::Button::WheelUp || mouse->button == MouseEvent::Button::WheelDown) {
        constexpr int    kWheelScrollLines = 3;
        const std::size_t rowCount          = BuildRows().size();
        const int          contentHeight     = std::max(0, size().height - kHeaderHeight - kBottomBorderHeight);
        const int          maxScroll         = std::max(0, static_cast<int>(rowCount) - contentHeight);
        if (mouse->button == MouseEvent::Button::WheelDown) {
            scrollOffset_ = std::min(scrollOffset_ + kWheelScrollLines, maxScroll);
        }
        else {
            scrollOffset_ = std::max(scrollOffset_ - kWheelScrollLines, 0);
        }
        return true;
    }

    if (mouse->button != MouseEvent::Button::Left || mouse->motion != MouseEvent::Motion::Pressed) {
        return false;
    }
    if (!model_) {
        return true;
    }
    if (mouse->at.y < kHeaderHeight || mouse->at.y >= size().height - kBottomBorderHeight) {
        return true; // chrome, not content
    }

    const std::vector<Row> rows  = BuildRows();
    const std::size_t      index = static_cast<std::size_t>(scrollOffset_ + (mouse->at.y - kHeaderHeight));
    if (index >= rows.size()) {
        return true;
    }
    const Row& row = rows[index];
    if (!row.isHeader) {
        return true; // only a hunk's own header row is clickable
    }
    constexpr int contentLeft = 1;
    if (mouse->at.x < contentLeft || mouse->at.x >= contentLeft + AffordanceWidth()) {
        return true; // click landed past the [stage]/[unstage] label itself
    }
    if (onHunkStageToggle_) {
        const editor::vcs::DiffHunkText& hunk = model_->hunks[row.hunkIndex];
        onHunkStageToggle_(model_->path, hunk.newStart, !model_->staged);
    }
    return true;
}

} // namespace ned::ui
