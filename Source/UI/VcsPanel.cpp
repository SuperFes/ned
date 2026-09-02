#include "VcsPanel.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <unordered_map>
#include <utility>

#include "Border.h"
#include "Editor/Key.h"
#include "Editor/ProjectRoot.h"
#include "KeyTranslation.h"
#include "Text/BinaryDetect.h"
#include "Text/ThreeWayMerge.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    constexpr int                       kMinPanelWidth = 4; // ProjectSidebar's own kMinSidebarWidth floor
    constexpr std::chrono::milliseconds kRefreshThrottle{1000}; // own poll cadence, independent of ProjectSidebar's

    constexpr int kHeaderHeight       = 1;
    constexpr int kBottomBorderHeight = 1;

    constexpr char32_t kCollapsedTriangle = U'▸'; // matches ProjectSidebar's own collapsed-strip hint glyph
    constexpr char32_t kExpandedTriangle  = U'▾';

    // A directory-tree node built from a known status-entry path list rather
    // than a disk walk -- see this file's own header comment on
    // BuildStatusTree below.
    struct TreeBuilderNode {
        std::filesystem::path                  fullPath;
        bool                                   isDirectory = false;
        editor::vcs::VcsRowStatus              status      = editor::vcs::VcsRowStatus::None;
        std::map<std::string, TreeBuilderNode> children; // key = this child's own filename, sorted alphabetically
    };

    void InsertStatusPath(TreeBuilderNode& root, const std::filesystem::path& rootPath,
                          const std::filesystem::path& absPath, editor::vcs::VcsRowStatus status) {
        std::vector<std::filesystem::path> chain;
        std::filesystem::path              p = absPath;
        while (p != rootPath) {
            chain.push_back(p);
            const std::filesystem::path parent = p.parent_path();
            if (parent == p) {
                return; // not under rootPath at all
            }
            p = parent;
        }
        std::reverse(chain.begin(), chain.end());

        TreeBuilderNode* cur = &root;
        for (std::size_t i = 0; i < chain.size(); ++i) {
            const std::filesystem::path& segPath = chain[i];
            const bool                   isLeaf  = (i + 1 == chain.size());
            TreeBuilderNode&              child   = cur->children[segPath.filename().string()];
            child.fullPath                        = segPath;
            child.isDirectory                     = !isLeaf;
            if (isLeaf) {
                child.status = status;
            }
            cur = &child;
        }
    }

    // Depth-first flatten, directories before files within each node --
    // std::map already sorts children alphabetically by filename, so two
    // filtered passes over the same map give "directories first, each group
    // alphabetical" for free, matching BuildProjectTree's own documented
    // ordering.
    void FlattenNode(const TreeBuilderNode& node, int depth, std::vector<editor::ProjectTreeEntry>& out,
                     std::unordered_map<std::filesystem::path, editor::vcs::VcsRowStatus>& statusOut) {
        for (const auto& [key, child] : node.children) {
            if (!child.isDirectory) {
                continue;
            }
            out.push_back(editor::ProjectTreeEntry{child.fullPath, depth, true});
            FlattenNode(child, depth + 1, out, statusOut);
        }
        for (const auto& [key, child] : node.children) {
            if (child.isDirectory) {
                continue;
            }
            out.push_back(editor::ProjectTreeEntry{child.fullPath, depth, false});
            statusOut[child.fullPath] = child.status;
        }
    }

    // Builds a directory tree (ProjectSidebar's own BuildProjectTree shape)
    // from a flat VcsStatusEntry list instead of a disk walk -- see this
    // file's own header comment. Returns the flattened depth-first entry
    // list plus a per-file status lookup (directory entries have no
    // meaningful status of their own in this panel -- unlike ProjectSidebar,
    // core-slice v1 doesn't roll a directory's status up from its
    // descendants).
    std::pair<std::vector<editor::ProjectTreeEntry>, std::unordered_map<std::filesystem::path, editor::vcs::VcsRowStatus>>
    BuildStatusTree(const std::vector<editor::vcs::VcsStatusEntry>& entries, const std::filesystem::path& root) {
        TreeBuilderNode builderRoot;
        builderRoot.fullPath    = root;
        builderRoot.isDirectory = true;
        for (const editor::vcs::VcsStatusEntry& entry : entries) {
            const std::filesystem::path absPath = (root / entry.path).lexically_normal();
            InsertStatusPath(builderRoot, root, absPath, editor::vcs::ClassifyPorcelainStatus(entry.state));
        }
        std::vector<editor::ProjectTreeEntry>                                  flat;
        std::unordered_map<std::filesystem::path, editor::vcs::VcsRowStatus> status;
        FlattenNode(builderRoot, 0, flat, status);
        return {std::move(flat), std::move(status)};
    }

    std::string SectionLabel(VcsPanelSection section, std::size_t count) {
        const char* name = section == VcsPanelSection::Staged     ? "Staged"
                          : section == VcsPanelSection::Unstaged ? "Unstaged"
                          : section == VcsPanelSection::Untracked ? "Untracked"
                                                                   : "Stashes";
        return std::string(name) + " (" + std::to_string(count) + ")";
    }

    // Same four-bucket coloring ProjectSidebar's own VcsStatusColor uses --
    // kept as a separate copy here rather than shared, since the two
    // widgets are free to diverge on presentation even though the
    // classification underneath (Editor/Vcs/VcsRowStatus.h) is shared.
    std::optional<Color> VcsStatusColor(editor::vcs::VcsRowStatus status) {
        switch (status) {
            case editor::vcs::VcsRowStatus::Deleted:
                return Color::BrightRed;
            case editor::vcs::VcsRowStatus::Modified:
                return Color::BrightBlue;
            case editor::vcs::VcsRowStatus::Added:
                return Color::BrightGreen;
            case editor::vcs::VcsRowStatus::Untracked:
                return Color::BrightCyan;
            case editor::vcs::VcsRowStatus::None:
                return std::nullopt;
        }
        return std::nullopt;
    }

    // Filenames are treated as ASCII-ish, same simplification ProjectSidebar's
    // own ToCodepoints makes.
    std::u32string ToCodepoints(const std::string& text) {
        std::u32string out;
        for (const char ch : text) {
            out += static_cast<char32_t>(static_cast<unsigned char>(ch));
        }
        return out;
    }

} // namespace

VcsPanel::VcsPanel(std::function<ActiveBuffer&()> activeBufferProvider, text::BufferList& bufferList,
                   std::string& statusMessage, const Theme& theme) : activeBufferProvider_(std::move(activeBufferProvider)),
                                                                       bufferList_(bufferList), statusMessage_(statusMessage), theme_(theme) {
}

int VcsPanel::Width() const {
    return collapsed_ ? 1 : width_;
}

void VcsPanel::SetWidth(int width) {
    width_ = std::max(kMinPanelWidth, width);
}

bool VcsPanel::Collapsed() const {
    return collapsed_;
}

void VcsPanel::SetCollapsed(bool collapsed) {
    collapsed_ = collapsed;
    if (collapsed_ && resizing_) {
        EndResize();
    }
    if (collapsed_ && Focused() && onFocusReturn_) {
        collapseOnFocusReturn_ = false;
        onFocusReturn_();
    }
}

void VcsPanel::ToggleCollapsed() {
    CommitCollapsed(!collapsed_);
}

void VcsPanel::CommitCollapsed(bool collapsed) {
    SetCollapsed(collapsed);
    if (onCollapseCommitted_) {
        onCollapseCommitted_(collapsed_);
    }
}

int VcsPanel::ExpandedWidth() const {
    return width_;
}

void VcsPanel::SetOnWidthCommitted(std::function<void(int)> handler) {
    onWidthCommitted_ = std::move(handler);
}

void VcsPanel::SetOnCollapseCommitted(std::function<void(bool)> handler) {
    onCollapseCommitted_ = std::move(handler);
}

void VcsPanel::SetOnFocusReturn(std::function<void()> handler) {
    onFocusReturn_ = std::move(handler);
}

void VcsPanel::SetOnAction(std::function<void(VcsPanelAction)> handler) {
    onAction_ = std::move(handler);
}

void VcsPanel::SetOnSelectionChanged(std::function<void(std::optional<std::filesystem::path>, bool)> handler) {
    onSelectionChanged_ = std::move(handler);
}

void VcsPanel::ForceRefresh() {
    RefreshStatus(/*force=*/true);
}

void VcsPanel::NotifySelectionChanged() {
    if (!onSelectionChanged_) {
        return;
    }
    const std::vector<Row> rows = BuildRows();
    std::optional<std::pair<std::filesystem::path, bool>> current;
    if (static_cast<std::size_t>(selectedIndex_) < rows.size()) {
        const Row& row = rows[static_cast<std::size_t>(selectedIndex_)];
        if (row.kind == Row::Kind::Entry && !row.entry.isDirectory &&
            (row.section == VcsPanelSection::Staged || row.section == VcsPanelSection::Unstaged)) {
            current = std::make_pair(row.entry.path, row.section == VcsPanelSection::Staged);
        }
    }
    if (current == lastNotifiedSelection_) {
        return;
    }
    lastNotifiedSelection_ = current;
    if (current) {
        onSelectionChanged_(current->first, current->second);
    }
    else {
        onSelectionChanged_(std::nullopt, false);
    }
}

void VcsPanel::TakeKeyboardFocus() {
    collapseOnFocusReturn_ = collapsed_;
    SetCollapsed(false);
    TakeFocus();
}

void VcsPanel::ReturnFocus() {
    const bool recollapse  = collapseOnFocusReturn_;
    collapseOnFocusReturn_ = false;
    if (onFocusReturn_) {
        onFocusReturn_();
    }
    if (recollapse) {
        SetCollapsed(true);
    }
}

int VcsPanel::ContentHeight() const {
    return std::max(0, size().height - kHeaderHeight - kBottomBorderHeight);
}

void VcsPanel::SetVcsRunner(editor::vcs::VcsRunner* vcsRunner) {
    vcsRunner_ = vcsRunner;
}

void VcsPanel::DispatchVcsStatusForTesting(const std::vector<editor::vcs::VcsStatusEntry>& entries) {
    sections_   = editor::vcs::PartitionVcsStatus(entries);
    haveStatus_ = true;
}

void VcsPanel::RefreshStatus(bool force) {
    if (!vcsRunner_) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!force && haveStatus_ && (now - lastRefreshTime_) < kRefreshThrottle) {
        return;
    }
    // lastRefreshTime_ only advances on a real success (below), not here --
    // confirmed live: ProjectSidebar polls VcsRunner::RequestStatus on its
    // own independent 500ms timer against the identical "status:"+root key
    // VcsRunner (Editor/Vcs/VcsRunner.cpp) single-flights, and since
    // ProjectSidebar paints first in main.cpp's composition (ahead of this
    // widget in bufferRow's child order), it wins that race almost every
    // time a request actually gets attempted -- both widgets are painted
    // from the same keypress-triggered frame, and 1000ms being an exact
    // multiple of ProjectSidebar's 500ms made the collision close to
    // deterministic in practice, not occasional. Advancing the timestamp
    // unconditionally (the original code) meant a lost race went silent
    // for a full new throttle window every time -- observed live as
    // sections_/stashes_/aheadBehind_ never updating at all outside a
    // force=true refresh (stage/commit/etc.'s own onSuccess). Leaving it
    // unadvanced on failure makes the very next Paint() retry immediately
    // instead, which is what actually converges in practice.
    vcsRunner_->RequestStatus(
        [this](std::vector<editor::vcs::VcsStatusEntry> entries) {
            sections_        = editor::vcs::PartitionVcsStatus(entries);
            haveStatus_      = true;
            lastRefreshTime_ = std::chrono::steady_clock::now();
            RefreshConflictedPaths();
        },
        [](const std::string&) {});
    vcsRunner_->RequestBranchList(
        [this](std::vector<editor::vcs::VcsBranchEntry> entries) {
            for (const editor::vcs::VcsBranchEntry& entry : entries) {
                if (entry.current) {
                    currentBranch_ = entry.name;
                    return;
                }
            }
        },
        [](const std::string&) {});
    // Stash support: same throttled cadence, silently keeps stashes_'s last-
    // known contents on error (a provider without stash vocabulary just
    // never shows the section, ParseStashList/PartitionVcsStatus's own
    // "highlighting is only meaningful when it works" precedent).
    vcsRunner_->RequestStashList([this](std::vector<editor::vcs::VcsStashEntry> entries) { stashes_ = std::move(entries); },
                                 [](const std::string&) {});
    // Ahead/behind summary: silently keeps the last-known value on error
    // (no upstream configured is the common, expected case for a repo with
    // no remote at all) -- same "highlighting is only meaningful when it
    // works" precedent as the rest of this refresh.
    vcsRunner_->RequestAheadBehind([this](editor::vcs::VcsAheadBehind ab) { aheadBehind_ = ab; }, [](const std::string&) {});
}

void VcsPanel::RefreshConflictedPaths() {
    conflictedPaths_.clear();
    const std::filesystem::path root = editor::ProjectRoot();
    const auto                  scan = [&](const std::vector<editor::vcs::VcsStatusEntry>& entries) {
        for (const editor::vcs::VcsStatusEntry& entry : entries) {
            const std::filesystem::path absPath = (root / entry.path).lexically_normal();
            std::ifstream                file(absPath, std::ios::binary);
            if (!file) {
                continue;
            }
            const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (text::HasConflictMarkers(content)) {
                conflictedPaths_.insert(absPath);
            }
        }
    };
    scan(sections_.staged);
    scan(sections_.unstaged);
}

std::vector<VcsPanel::Row> VcsPanel::BuildRows() const {
    std::vector<Row>            rows;
    const std::filesystem::path root = editor::ProjectRoot();

    const auto addSection = [&](VcsPanelSection section, const std::vector<editor::vcs::VcsStatusEntry>& entries) {
        Row header;
        header.kind      = Row::Kind::SectionHeader;
        header.section   = section;
        header.fileCount = entries.size();
        rows.push_back(header);

        if (collapsedSections_.contains(section) || entries.empty()) {
            return;
        }

        const auto [flat, statusByPath] = BuildStatusTree(entries, root);

        // Skip subtrees under a collapsed directory -- VisibleEntries'
        // exact filtering rule (ProjectSidebar.cpp).
        int skipBelowDepth = -1;
        for (const editor::ProjectTreeEntry& entry : flat) {
            if (skipBelowDepth != -1) {
                if (entry.depth > skipBelowDepth) {
                    continue;
                }
                skipBelowDepth = -1;
            }
            Row row;
            row.kind    = Row::Kind::Entry;
            row.section = section;
            row.entry   = entry;
            if (!entry.isDirectory) {
                const auto it = statusByPath.find(entry.path);
                row.status     = it != statusByPath.end() ? it->second : editor::vcs::VcsRowStatus::None;
                row.conflicted = conflictedPaths_.contains(entry.path);
            }
            rows.push_back(row);
            if (entry.isDirectory && !expandedDirs_.contains(entry.path)) {
                skipBelowDepth = entry.depth;
            }
        }
    };

    addSection(VcsPanelSection::Staged, sections_.staged);
    addSection(VcsPanelSection::Unstaged, sections_.unstaged);
    addSection(VcsPanelSection::Untracked, sections_.untracked);

    // Stash support: unlike the three sections above, this one is only
    // shown when non-empty -- ROADMAP's own "shown only when non-empty"
    // call, since an empty stash list is the overwhelmingly common case and
    // permanently reserving a row for it would be pure clutter.
    if (!stashes_.empty()) {
        Row header;
        header.kind      = Row::Kind::SectionHeader;
        header.section   = VcsPanelSection::Stash;
        header.fileCount = stashes_.size();
        rows.push_back(header);
        if (!collapsedSections_.contains(VcsPanelSection::Stash)) {
            for (const editor::vcs::VcsStashEntry& entry : stashes_) {
                Row row;
                row.kind    = Row::Kind::StashEntry;
                row.section = VcsPanelSection::Stash;
                row.stash   = entry;
                rows.push_back(row);
            }
        }
    }

    return rows;
}

void VcsPanel::Paint(Canvas c) {
    RefreshStatus(/*force=*/false);

    // generic-popup follow-up's own precedent (ProjectSidebar::Paint): reset
    // the whole Brush per cell, not just background, so a prior overlay
    // frame can never leave a stale foreground baked into a blank row.
    const Brush blankBrush{.background = theme_.background, .foreground = theme_.defaultForeground};
    for (int row = 0; row < c.size().height; ++row) {
        for (int col = 0; col < c.size().width; ++col) {
            Cell& cell     = c[{.x = col, .y = row}];
            cell.character = " ";
            blankBrush.ApplyTo(cell);
        }
    }

    if (collapsed_) {
        const std::string line = text::EncodeCodepointUtf8(U'│');
        for (int row = 0; row < c.size().height; ++row) {
            Cell& cell     = c[{.x = 0, .y = row}];
            cell.character = line;
            theme_.border.ApplyTo(cell);
        }
        Cell& hint     = c[{.x = 0, .y = 0}];
        hint.character = text::EncodeCodepointUtf8(kCollapsedTriangle);
        theme_.borderAccent.ApplyTo(hint);
        return;
    }

    const Brush frameBrush = (resizing_ || Focused()) ? theme_.borderAccent : theme_.border;
    DrawBorder(c, frameBrush);

    std::string title;
    if (pendingRevertConfirm_) {
        // Discard/revert: the confirm prompt replaces the title outright
        // while pending -- the one destructive action here gets real
        // friction, unlike stage/unstage.
        title = "Discard changes to " + pendingRevertConfirm_->filename().string() + "? y/n";
    }
    else {
        // Branch switcher/creator inline: the checked-out branch and staged
        // count ride the border title (ProjectSidebar's own header-row
        // precedent) rather than a dedicated content row -- see
        // currentBranch_'s own doc comment.
        title = "VCS";
        if (currentBranch_) {
            title += " · " + *currentBranch_;
        }
        // Ahead/behind: arrows only when known and actually non-zero --
        // keeps the common "up to date"/"no upstream" case uncluttered.
        if (aheadBehind_ && (aheadBehind_->ahead > 0 || aheadBehind_->behind > 0)) {
            title += " ↑" + std::to_string(aheadBehind_->ahead) + " ↓" + std::to_string(aheadBehind_->behind);
        }
        if (!sections_.staged.empty()) {
            title += " · " + std::to_string(sections_.staged.size()) + " staged";
        }
    }
    DrawBorderTitle(c, title, theme_.borderAccent);

    const int contentLeft    = 1;
    const int contentColumns = std::max(0, c.size().width - 2);
    const int contentHeight  = ContentHeight();

    const std::vector<Row> rows = BuildRows();
    if (!rows.empty()) {
        selectedIndex_ = std::clamp(selectedIndex_, 0, static_cast<int>(rows.size()) - 1);
    }
    const bool focused = Focused();

    for (int contentRow = 0; contentRow < contentHeight; ++contentRow) {
        const std::size_t index = static_cast<std::size_t>(scrollOffset_ + contentRow);
        if (index >= rows.size()) {
            break;
        }
        const Row& row = rows[index];
        const int  y   = contentRow + kHeaderHeight;

        const bool isSelectedRow = focused && static_cast<int>(index) == selectedIndex_;

        std::u32string label;
        Brush           brush;

        if (row.kind == Row::Kind::SectionHeader) {
            // ToCodepoints treats its input as ASCII-ish (one byte, one
            // codepoint -- see its own doc comment); the disclosure
            // triangle is a real multi-byte UTF-8 glyph, so it's appended
            // as an actual char32_t, never routed through ToCodepoints
            // itself. Confirmed live: doing this via a plain UTF-8-encoded
            // std::string mojibake'd the triangle into garbage.
            label = std::u32string(1, collapsedSections_.contains(row.section) ? kCollapsedTriangle : kExpandedTriangle);
            label += U' ';
            label += ToCodepoints(SectionLabel(row.section, row.fileCount));
            brush = theme_.tabBar;
        }
        else if (row.kind == Row::Kind::StashEntry) {
            label = U"  " + ToCodepoints(row.stash.ref) + U": " + ToCodepoints(row.stash.message);
            brush = Brush{.background = theme_.background, .foreground = theme_.lineNumberForeground};
        }
        else {
            const bool marked = !row.entry.isDirectory && selected_.contains(row.entry.path);
            std::u32string indent(static_cast<std::size_t>(row.entry.depth) * 2, U' ');
            label = indent;
            // Real ballot-box glyphs (U+2610/U+2611 -- single-column BMP,
            // the same "plain Unicode box-drawing family, no Nerd Font
            // dependency" convention ProjectSidebar's own disclosure
            // triangles/tree connectors already establish), not bracketed
            // ASCII -- see OnEvent's own checkbox-column click handling
            // just below for the matching mouse target.
            label += marked ? U"☑ " : (row.entry.isDirectory ? U"" : U"☐ ");
            if (row.entry.isDirectory) {
                label += expandedDirs_.contains(row.entry.path) ? kExpandedTriangle : kCollapsedTriangle;
                label += U' ';
            }
            label += ToCodepoints(row.entry.path.filename().string());
            if (row.entry.isDirectory) {
                label += U'/';
            }
            if (row.conflicted) {
                // Appended, not prefixed -- keeps OnEvent's fixed checkbox-
                // column math (contentLeft + depth*2) undisturbed regardless
                // of conflict state.
                label += U" ⚠";
            }
            const std::optional<Color> statusColor = VcsStatusColor(row.status);
            brush = Brush{.background = theme_.background,
                          .foreground = statusColor.value_or(row.entry.isDirectory ? theme_.lineNumberForeground
                                                                                    : theme_.defaultForeground)};
        }

        if (isSelectedRow) {
            brush.background = theme_.selectionBackground;
            for (int x = contentLeft; x < contentLeft + contentColumns; ++x) {
                c[{.x = x, .y = y}].background_color = theme_.selectionBackground;
            }
        }

        for (std::size_t i = 0; i < label.size() && static_cast<int>(i) < contentColumns; ++i) {
            Cell& cell     = c[{.x = contentLeft + static_cast<int>(i), .y = y}];
            cell.character = text::EncodeCodepointUtf8(label[i]);
            brush.ApplyTo(cell);
        }
    }
}

bool VcsPanel::OnEvent(const Event& event) {
    if (!event.is_mouse()) {
        if (!Focused()) {
            return false;
        }
        return HandleKeyEvent(event);
    }
    const MouseEvent rawMouse = event.mouse();

    if (rawMouse.motion == MouseEvent::Motion::Moved && resizing_) {
        UpdateResize(rawMouse.at.x);
        return true;
    }
    if (rawMouse.motion == MouseEvent::Motion::Released && resizing_) {
        EndResize();
        return true;
    }

    const auto mouse = LocalMouseEvent(event);
    if (!mouse) {
        return false;
    }

    if (collapsed_) {
        if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
            CommitCollapsed(false);
        }
        return true;
    }

    if (mouse->button == MouseEvent::Button::WheelUp || mouse->button == MouseEvent::Button::WheelDown) {
        constexpr int    kWheelScrollLines = 3;
        const std::size_t rowCount          = BuildRows().size();
        const int          maxScroll         = std::max(0, static_cast<int>(rowCount) - ContentHeight());
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

    if (mouse->at.x == size().width - 1) {
        BeginResize(rawMouse.at.x);
        return true;
    }

    if (mouse->at.y < kHeaderHeight || mouse->at.y >= size().height - kBottomBorderHeight) {
        return true; // chrome, not content
    }

    const std::vector<Row> rows = BuildRows();
    const std::size_t      index = static_cast<std::size_t>(scrollOffset_ + (mouse->at.y - kHeaderHeight));
    if (index >= rows.size()) {
        return true;
    }
    selectedIndex_ = static_cast<int>(index);
    NotifySelectionChanged();
    const Row& row = rows[index];

    if (row.kind == Row::Kind::SectionHeader) {
        if (collapsedSections_.contains(row.section)) {
            collapsedSections_.erase(row.section);
        }
        else {
            collapsedSections_.insert(row.section);
        }
        return true;
    }
    if (row.kind == Row::Kind::StashEntry) {
        PopStash(row.stash.ref);
        return true;
    }
    if (row.entry.isDirectory) {
        ToggleDirectory(row.entry.path);
    }
    else {
        // A click squarely on the ☐/☑ glyph toggles the mark instead of
        // opening the file -- same column math Paint() builds the label
        // with (contentLeft=1, then depth*2 columns of indent before the
        // checkbox glyph itself).
        constexpr int contentLeft    = 1;
        const int     checkboxColumn = contentLeft + row.entry.depth * 2;
        if (mouse->at.x == checkboxColumn) {
            if (selected_.contains(row.entry.path)) {
                selected_.erase(row.entry.path);
            }
            else {
                selected_.insert(row.entry.path);
            }
        }
        else {
            OpenFileEntry(row.entry.path);
        }
    }
    return true;
}

void VcsPanel::ToggleDirectory(const std::filesystem::path& path) {
    if (expandedDirs_.contains(path)) {
        expandedDirs_.erase(path);
    }
    else {
        expandedDirs_.insert(path);
    }
    const std::size_t rowCount = BuildRows().size();
    if (rowCount > 0 && scrollOffset_ >= static_cast<int>(rowCount)) {
        scrollOffset_ = static_cast<int>(rowCount) - 1;
    }
}

void VcsPanel::OpenFileEntry(const std::filesystem::path& path) {
    try {
        text::Buffer& opened = bufferList_.OpenOrCreateFile(path);
        activeBufferProvider_().Set(opened);
        statusMessage_.clear();
        if (conflictedPaths_.contains(path.lexically_normal())) {
            // Conflict-file affordance: jump to the first real conflict
            // marker rather than leaving point at the top -- a small local
            // scan (line-start "<<<<<<<" only, not mid-line text that
            // happens to contain it), no new shared infrastructure.
            const std::string content = opened.Text();
            std::size_t       pos     = content.find("<<<<<<<");
            while (pos != std::string::npos && pos != 0 && content[pos - 1] != '\n') {
                pos = content.find("<<<<<<<", pos + 1);
            }
            if (pos != std::string::npos) {
                opened.SetPoint(pos);
            }
        }
    }
    catch (const text::BinaryFileError&) {
        statusMessage_ = "\"" + path.string() + "\" looks like a binary file.";
    }
    catch (const std::exception& e) {
        statusMessage_ = e.what();
    }
}

void VcsPanel::PushStash() {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    vcsRunner_->RequestStashPush(
        "", [this] { RefreshStatus(/*force=*/true); },
        [this](const std::string& error) { statusMessage_ = "vcs: " + error; });
}

void VcsPanel::PopStash(const std::string& ref) {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    vcsRunner_->RequestStashPop(
        ref, [this] { RefreshStatus(/*force=*/true); },
        [this](const std::string& error) { statusMessage_ = "vcs: " + error; });
}

void VcsPanel::DropStash(const std::string& ref) {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    vcsRunner_->RequestStashDrop(
        ref, [this] { RefreshStatus(/*force=*/true); },
        [this](const std::string& error) { statusMessage_ = "vcs: " + error; });
}

void VcsPanel::RunRemoteAction(RemoteAction action) {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    auto onSuccess = [this] {
        statusMessage_.clear();
        RefreshStatus(/*force=*/true);
    };
    auto onError = [this](const std::string& error) { statusMessage_ = "vcs: " + error; };
    switch (action) {
        case RemoteAction::Fetch:
            vcsRunner_->RequestFetch(onSuccess, onError);
            return;
        case RemoteAction::Pull:
            vcsRunner_->RequestPull(onSuccess, onError);
            return;
        case RemoteAction::Push:
            vcsRunner_->RequestPush(onSuccess, onError);
            return;
    }
}

void VcsPanel::StageOrUnstageSelectionOrFocused(bool stage) {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    std::vector<std::filesystem::path> targets;
    if (!selected_.empty()) {
        targets.assign(selected_.begin(), selected_.end());
    }
    else {
        const std::vector<Row> rows = BuildRows();
        if (static_cast<std::size_t>(selectedIndex_) < rows.size()) {
            const Row& row = rows[static_cast<std::size_t>(selectedIndex_)];
            if (row.kind == Row::Kind::Entry && !row.entry.isDirectory) {
                targets.push_back(row.entry.path);
            }
        }
    }
    if (targets.empty()) {
        return;
    }

    pendingBatchOps_ += static_cast<int>(targets.size());
    for (const std::filesystem::path& path : targets) {
        selected_.erase(path);
        auto onDone = [this]() {
            --pendingBatchOps_;
            if (pendingBatchOps_ <= 0) {
                pendingBatchOps_ = 0;
                RefreshStatus(/*force=*/true);
            }
        };
        auto onError = [this](const std::string& error) {
            statusMessage_ = "vcs: " + error;
            --pendingBatchOps_;
            if (pendingBatchOps_ <= 0) {
                pendingBatchOps_ = 0;
                RefreshStatus(/*force=*/true);
            }
        };
        if (stage) {
            vcsRunner_->RequestStage(path, onDone, onError);
        }
        else {
            vcsRunner_->RequestUnstage(path, onDone, onError);
        }
    }
}

void VcsPanel::EnsureSelectionVisible() {
    const int contentHeight = ContentHeight();
    if (contentHeight <= 0) {
        return;
    }
    if (selectedIndex_ < scrollOffset_) {
        scrollOffset_ = selectedIndex_;
    }
    else if (selectedIndex_ >= scrollOffset_ + contentHeight) {
        scrollOffset_ = selectedIndex_ - contentHeight + 1;
    }
}

bool VcsPanel::HandleKeyEvent(const Event& event) {
    const auto chord = TranslateKey(event);
    if (!chord) {
        return true;
    }
    const bool cancel = chord->Special == editor::SpecialKey::Escape || (chord->Control && chord->Codepoint == U'g');

    // Discard/revert confirm state intercepts every key while pending,
    // before any of the normal navigation/action handling below --
    // matching BufferView's own y/n confirmation shape (ConfirmOverwriteSave
    // etc.), just implemented locally since this widget has no
    // MinibufferPrompt of its own.
    if (pendingRevertConfirm_) {
        // Only a bare 'y'/'Y' confirms -- deliberately not Enter too, real
        // "are you sure" friction for the one destructive action in this
        // panel (matches the ROADMAP's own explicit call for this).
        // Anything else, including Escape/'n', cancels.
        const bool confirmed = chord->Special == editor::SpecialKey::None && !chord->Control && !chord->Meta &&
                               (chord->Codepoint == U'y' || chord->Codepoint == U'Y');
        const std::filesystem::path target = *pendingRevertConfirm_;
        pendingRevertConfirm_.reset();
        if (confirmed) {
            if (vcsRunner_) {
                vcsRunner_->RequestRevert(
                    target, [this] { RefreshStatus(/*force=*/true); },
                    [this](const std::string& error) { statusMessage_ = "vcs: " + error; });
            }
            else {
                statusMessage_ = "no vcs runner configured";
            }
        }
        return true;
    }

    const std::vector<Row> rows = BuildRows();
    if (rows.empty()) {
        if (cancel) {
            ReturnFocus();
        }
        return true;
    }
    selectedIndex_ = std::clamp(selectedIndex_, 0, static_cast<int>(rows.size()) - 1);
    const Row& row = rows[static_cast<std::size_t>(selectedIndex_)];

    const bool up   = chord->Special == editor::SpecialKey::Up || (chord->Control && chord->Codepoint == U'p');
    const bool down = chord->Special == editor::SpecialKey::Down || (chord->Control && chord->Codepoint == U'n');
    if (up || down) {
        selectedIndex_ = std::clamp(selectedIndex_ + (down ? 1 : -1), 0, static_cast<int>(rows.size()) - 1);
        EnsureSelectionVisible();
        NotifySelectionChanged();
        return true;
    }

    if (chord->Special == editor::SpecialKey::Enter) {
        if (row.kind == Row::Kind::SectionHeader) {
            if (collapsedSections_.contains(row.section)) {
                collapsedSections_.erase(row.section);
            }
            else {
                collapsedSections_.insert(row.section);
            }
        }
        else if (row.kind == Row::Kind::StashEntry) {
            PopStash(row.stash.ref);
        }
        else if (row.entry.isDirectory) {
            ToggleDirectory(row.entry.path);
        }
        else {
            OpenFileEntry(row.entry.path);
            ReturnFocus();
        }
        return true;
    }
    if (chord->Special == editor::SpecialKey::Right) {
        if (row.kind == Row::Kind::Entry && row.entry.isDirectory && !expandedDirs_.contains(row.entry.path)) {
            ToggleDirectory(row.entry.path);
        }
        return true;
    }
    if (chord->Special == editor::SpecialKey::Left) {
        if (row.kind == Row::Kind::Entry && row.entry.isDirectory && expandedDirs_.contains(row.entry.path)) {
            ToggleDirectory(row.entry.path);
        }
        return true;
    }
    if (chord->Special == editor::SpecialKey::None && chord->Codepoint == U' ' && !chord->Control) {
        if (row.kind == Row::Kind::Entry && !row.entry.isDirectory) {
            if (selected_.contains(row.entry.path)) {
                selected_.erase(row.entry.path);
            }
            else {
                selected_.insert(row.entry.path);
            }
        }
        return true;
    }
    if (chord->Special == editor::SpecialKey::None && !chord->Control && !chord->Meta) {
        if (chord->Codepoint == U'a') {
            StageOrUnstageSelectionOrFocused(/*stage=*/true);
            return true;
        }
        if (chord->Codepoint == U'u') {
            StageOrUnstageSelectionOrFocused(/*stage=*/false);
            return true;
        }
        if (chord->Codepoint == U'x') {
            // Discard/revert: dired's own "delete/discard" mnemonic. Only
            // meaningful on a real file row -- a section header or
            // directory has nothing to revert.
            if (row.kind == Row::Kind::Entry && !row.entry.isDirectory) {
                pendingRevertConfirm_ = row.entry.path;
            }
            return true;
        }
        if (chord->Codepoint == U'z') {
            // Stash support: Magit's own real-world mnemonic for its stash
            // prefix -- stashes the whole working tree (not row-scoped),
            // meaningful from anywhere in the panel.
            PushStash();
            return true;
        }
        if (chord->Codepoint == U'd') {
            // Drop the focused stash entry -- meaningless anywhere else.
            if (row.kind == Row::Kind::StashEntry) {
                DropStash(row.stash.ref);
            }
            return true;
        }
        if (chord->Codepoint == U'f') {
            // Push/pull/fetch: Magit's own real-world 'f'/'F'/'P' mnemonics,
            // reused directly. Meaningful from anywhere in the panel (root-
            // scoped, not row-scoped); no confirm friction -- the least
            // destructive of the remote-touching actions here.
            RunRemoteAction(RemoteAction::Fetch);
            return true;
        }
        if (chord->Codepoint == U'F') {
            RunRemoteAction(RemoteAction::Pull);
            return true;
        }
        if (chord->Codepoint == U'P') {
            RunRemoteAction(RemoteAction::Push);
            return true;
        }
        if (chord->Codepoint == U'c' || chord->Codepoint == U'w' || chord->Codepoint == U'n') {
            // ReturnFocus() *before* firing onAction_ -- WindowManager::
            // RequestVcsPanelAction resolves "the focused pane"
            // (RequestOpenBinaryFile's own shape), and while this widget
            // itself still holds keyboard focus, no BufferView reports
            // Focused() at all, silently no-opping the request. Handing
            // focus back first (falling back to the first leaf if nothing
            // else was already focused -- WindowManager::TakeFocus's own
            // documented fallback) is what makes a real pane resolvable by
            // the time onAction_ actually runs. Confirmed live: the commit
            // trigger was a silent no-op before this fix.
            ReturnFocus();
            if (onAction_) {
                onAction_(chord->Codepoint == U'c'   ? VcsPanelAction::Commit
                         : chord->Codepoint == U'w' ? VcsPanelAction::SwitchBranch
                                                     : VcsPanelAction::CreateBranch);
            }
            return true;
        }
    }
    if (cancel) {
        ReturnFocus();
        return true;
    }
    return true;
}

bool VcsPanel::IsResizing() const {
    return resizing_;
}

void VcsPanel::BeginResize(int globalMouseX) {
    resizing_            = true;
    resizeAnchorGlobalX_ = globalMouseX;
    resizeAnchorWidth_   = size().width; // see ProjectSidebar::BeginResize's own comment on why not width_ directly
    resizeStartWidth_    = width_;
}

void VcsPanel::UpdateResize(int globalMouseX) {
    const int delta = globalMouseX - resizeAnchorGlobalX_;
    width_          = std::max(kMinPanelWidth, resizeAnchorWidth_ + delta);
}

void VcsPanel::EndResize() {
    if (!resizing_) {
        return;
    }
    resizing_ = false;
    if (width_ != resizeStartWidth_ && onWidthCommitted_) {
        onWidthCommitted_(width_);
    }
}

} // namespace ned::ui
