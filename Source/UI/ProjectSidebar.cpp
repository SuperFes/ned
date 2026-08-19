#include "ProjectSidebar.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <optional>
#include <system_error>

#include "Editor/ProjectRoot.h"
#include "Editor/ProjectTree.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    // Standard Unicode Box Drawing characters (guaranteed single-column
    // width in any monospace font) for tree-connector lines, `tree`-command
    // style -- deliberately not Nerd Font icons: those need a specific
    // patched font and render as boxes/mojibake without it, the same
    // portability concern already documented for the scroll bar's arrow
    // glyphs. Per-file-type emoji icons were considered too and rejected
    // for a second reason on top of that: most terminals render emoji as
    // double-width, which would break this widget's precise column-by-
    // column Cell placement. Disclosure triangles are the same BMP
    // "Geometric Shapes" family ScrollArrowButton's own ▲/▼ already use.
    constexpr char32_t kTreeContinue      = U'│';
    constexpr char32_t kTreeBranch        = U'├';
    constexpr char32_t kTreeLast          = U'└';
    constexpr char32_t kTreeDash          = U'─';
    constexpr char32_t kDividerLine       = U'│';
    constexpr char32_t kCollapsedTriangle = U'▸';
    constexpr char32_t kExpandedTriangle  = U'▾';

    // A floor on drag-resized width, not a real design limit -- just enough
    // to keep the divider itself and a sliver of content visible so the
    // handle never becomes unreachable. There's no matching hard ceiling:
    // the containing composition's own layout naturally shrinks an
    // over-large request back down to whatever the terminal actually has.
    constexpr int kMinSidebarWidth = 4;

    // sidebar-header follow-up: row 0 is always the project-name header,
    // never tree content -- see ProjectSidebar::Paint's own comment.
    constexpr int kHeaderHeight = 1;

    // See CachedTree()'s own comment (ProjectSidebar.h) for why this exists
    // at all. 500ms is a deliberately unscientific pick -- fast enough that
    // the sidebar never feels stale to a human, slow enough (well under
    // typical keystroke rate) to actually eliminate the per-keystroke
    // directory-walk cost that motivated this in the first place.
    constexpr std::chrono::milliseconds kTreeCacheThrottle{500};

    // How close together two clicks on the same file need to land to count
    // as a double click (single-click-preview follow-up) -- no built-in
    // double-click detection to defer to. A conventional desktop
    // double-click interval, not tuned against anything in this codebase.
    constexpr std::chrono::milliseconds kDoubleClickWindow{400};

    // Whether the ancestor at `level` still has a sibling entry appearing
    // later in the flat list, scanning forward from `fromIndex` -- i.e.
    // whether that ancestor's own vertical connector should keep drawing
    // (kTreeContinue) past this row, or has already closed (blank).
    // Depth-first order guarantees the first entry found at exactly `level`
    // (if any, before dropping to a shallower depth) answers this. Works
    // the same whether `entries` is the full tree or a collapse-filtered
    // subset: filtering only ever drops whole subtrees, never reorders or
    // partially-drops a directory's own direct children, so relative
    // sibling order is preserved either way.
    bool LevelContinues(const std::vector<editor::ProjectTreeEntry>& entries, std::size_t fromIndex, int level) {
        for (std::size_t j = fromIndex; j < entries.size(); ++j) {
            if (entries[j].depth < level) {
                return false;
            }
            if (entries[j].depth == level) {
                return true;
            }
        }
        return false;
    }

    bool IsLastSibling(const std::vector<editor::ProjectTreeEntry>& entries, std::size_t index) {
        return !LevelContinues(entries, index + 1, entries[index].depth);
    }

    // e.g. "│  │  ├─" for a third-level entry whose grandparent still has
    // more siblings coming but whose parent doesn't.
    std::u32string TreePrefix(const std::vector<editor::ProjectTreeEntry>& entries, std::size_t index) {
        const editor::ProjectTreeEntry& entry = entries[index];

        std::u32string prefix;
        for (int level = 0; level < entry.depth; ++level) {
            prefix += LevelContinues(entries, index + 1, level) ? kTreeContinue : U' ';
            prefix += U' ';
        }
        prefix += IsLastSibling(entries, index) ? kTreeLast : kTreeBranch;
        prefix += kTreeDash;
        return prefix;
    }

    // Filenames are treated as ASCII-ish here, same simplification
    // ModeLine's own buffer-name rendering already makes -- a genuinely
    // multi-byte-UTF-8 filename would render byte-by-byte, a known, narrow
    // limitation, not new to this widget.
    std::u32string ToCodepoints(const std::string& text) {
        std::u32string out;
        for (const char ch : text) {
            out += static_cast<char32_t>(static_cast<unsigned char>(ch));
        }
        return out;
    }

    std::u32string BuildLabel(const std::vector<editor::ProjectTreeEntry>& entries, std::size_t index,
                              const std::set<std::filesystem::path>& expandedDirs) {
        const editor::ProjectTreeEntry& entry = entries[index];

        std::u32string label = TreePrefix(entries, index);
        label += U' ';
        if (entry.isDirectory) {
            label += expandedDirs.contains(entry.path) ? kExpandedTriangle : kCollapsedTriangle;
            label += U' ';
        }
        label += ToCodepoints(entry.path.filename().string());
        if (entry.isDirectory) {
            label += U'/';
        }
        return label;
    }

    // The header row's own label -- the project root's directory name (e.g.
    // opening ~/dev/ned shows "ned"), matching VS Code's own workspace-
    // sidebar-header convention. filename() is empty for a root path like
    // "/" itself (no final path component to take), so this falls back to
    // the full path string in that case rather than showing a blank header.
    std::u32string ProjectNameLabel() {
        const std::filesystem::path root     = editor::ProjectRoot();
        const std::string           filename = root.filename().string();
        return ToCodepoints(filename.empty() ? root.string() : filename);
    }

    // Ancestor entries of entries[index], returned root-to-leaf (index 0 =
    // shallowest), found by scanning backward for the nearest preceding
    // entry at each successively shallower depth -- depth-first order
    // guarantees that's always the right one.
    std::vector<std::size_t> AncestorIndices(const std::vector<editor::ProjectTreeEntry>& entries, std::size_t index) {
        std::vector<std::size_t> ancestors;
        int                      neededDepth = entries[index].depth - 1;
        for (std::size_t j = index; j > 0 && neededDepth >= 0;) {
            --j;
            if (entries[j].depth == neededDepth) {
                ancestors.push_back(j);
                --neededDepth;
            }
        }
        std::reverse(ancestors.begin(), ancestors.end());
        return ancestors;
    }

    struct RowLayout {
        std::vector<std::size_t> stickyAncestors; // root-to-leaf, pinned at the top of the viewport
        std::size_t              scrollIndex = 0;
    };

    RowLayout ComputeRowLayout(const std::vector<editor::ProjectTreeEntry>& entries, int scrollOffset) {
        RowLayout layout;
        if (entries.empty()) {
            return layout;
        }
        layout.scrollIndex     = static_cast<std::size_t>(std::clamp(scrollOffset, 0, static_cast<int>(entries.size()) - 1));
        layout.stickyAncestors = AncestorIndices(entries, layout.scrollIndex);
        return layout;
    }

    // Which entry (by index into `entries`) row `row` of a
    // `viewportHeight`-tall viewport shows, given `layout`: the first
    // layout.stickyAncestors.size() rows (capped to the viewport height) are
    // the pinned ancestor chain, the rest are ordinary scrolled content
    // starting at layout.scrollIndex. Shared between Paint() and OnEvent()
    // so clicking a row always resolves to the same entry Paint() drew there.
    std::optional<std::size_t> EntryIndexAtRow(const RowLayout& layout, const std::vector<editor::ProjectTreeEntry>& entries,
                                               int viewportHeight, int row) {
        const int stickyCount = std::min<int>(static_cast<int>(layout.stickyAncestors.size()), viewportHeight);
        if (row < stickyCount) {
            return layout.stickyAncestors[static_cast<std::size_t>(row)];
        }
        const std::size_t index = layout.scrollIndex + static_cast<std::size_t>(row - stickyCount);
        if (index >= entries.size()) {
            return std::nullopt;
        }
        return index;
    }

} // namespace

ProjectSidebar::ProjectSidebar(std::function<ActiveBuffer&()> activeBufferProvider, text::BufferList& bufferList,
                               std::string& statusMessage, const Theme& theme) : activeBufferProvider_(std::move(activeBufferProvider)), bufferList_(bufferList), statusMessage_(statusMessage),
                                                                                 theme_(theme) {
}

std::vector<editor::ProjectTreeEntry> ProjectSidebar::VisibleEntries(const std::vector<editor::ProjectTreeEntry>& all) const {
    std::vector<editor::ProjectTreeEntry> visible;
    int                                   skipBelowDepth = -1;
    for (const editor::ProjectTreeEntry& entry : all) {
        if (skipBelowDepth != -1) {
            if (entry.depth > skipBelowDepth) {
                continue; // still inside a collapsed subtree
            }
            skipBelowDepth = -1;
        }
        visible.push_back(entry);
        if (entry.isDirectory && !expandedDirs_.contains(entry.path)) {
            skipBelowDepth = entry.depth;
        }
    }
    return visible;
}

int ProjectSidebar::Width() const {
    return width_;
}

int ProjectSidebar::ContentHeight() const {
    return std::max(0, size().height - kHeaderHeight);
}

const std::vector<editor::ProjectTreeEntry>& ProjectSidebar::CachedTree() {
    const std::filesystem::path root = editor::ProjectRoot();
    const auto                  now  = std::chrono::steady_clock::now();
    if (!treeCacheValid_ || root != treeCacheRoot_ || (now - treeCacheTime_) >= kTreeCacheThrottle) {
        treeCache_      = editor::BuildProjectTree(root);
        treeCacheRoot_  = root;
        treeCacheTime_  = now;
        treeCacheValid_ = true;
    }
    return treeCache_;
}

void ProjectSidebar::InvalidateTree() {
    treeCacheValid_ = false;
}

void ProjectSidebar::SetOnBufferClosed(std::function<void(text::Buffer&)> handler) {
    onBufferClosed_ = std::move(handler);
}

void ProjectSidebar::Paint(Canvas c) {
    for (int row = 0; row < c.size().height; ++row) {
        for (int col = 0; col < c.size().width; ++col) {
            Cell& cell            = c[{.x = col, .y = row}];
            cell.character        = " ";
            cell.background_color = theme_.background;
        }
    }

    // Rightmost column is reserved for the divider marking the sidebar/
    // buffer boundary -- content renders into [0, dividerColumn).
    const int dividerColumn = c.size().width - 1;

    // sidebar-header follow-up: row 0 is always the project root's own
    // directory name -- main.cpp's composition now gives ProjectSidebar the
    // row tabBar used to sit above instead (tabBar itself moved to sit only
    // above the pane area), matching VS Code's own per-workspace sidebar
    // header. Same chrome brush TabBar's own row and a sticky pinned
    // ancestor already use. Not yet clickable for anything beyond
    // consuming the event (see OnEvent's own comment) -- the intended hook
    // point for project-settings, once that exists, without needing to
    // touch this row/height bookkeeping again.
    {
        const std::u32string label = ProjectNameLabel();
        for (std::size_t i = 0; i < label.size() && static_cast<int>(i) < dividerColumn; ++i) {
            Cell& cell     = c[{.x = static_cast<int>(i), .y = 0}];
            cell.character = text::EncodeCodepointUtf8(label[i]);
            theme_.tabBar.ApplyTo(cell);
        }
    }

    const std::vector<editor::ProjectTreeEntry> entries    = VisibleEntries(CachedTree());
    const std::optional<std::filesystem::path>& activePath = activeBufferProvider_().Get().Path();

    const int       contentHeight = ContentHeight();
    const RowLayout layout        = ComputeRowLayout(entries, scrollOffset_);
    const int       stickyCount   = std::min<int>(static_cast<int>(layout.stickyAncestors.size()), contentHeight);

    for (int contentRow = 0; contentRow < contentHeight; ++contentRow) {
        const std::optional<std::size_t> index = EntryIndexAtRow(layout, entries, contentHeight, contentRow);
        if (!index) {
            break; // sticky rows always come first; once content runs out, so do all later rows
        }
        const editor::ProjectTreeEntry& entry    = entries[*index];
        const bool                      isSticky = contentRow < stickyCount;
        const int                       row      = contentRow + kHeaderHeight;

        const bool  isActiveFile = !entry.isDirectory && activePath && *activePath == entry.path;
        const Brush brush =
            isActiveFile ? theme_.activeTab
            : isSticky   ? theme_.tabBar // pinned ancestor header -- same chrome family as TabBar's own row
                         : Brush{.background = theme_.background,
                                 .foreground = entry.isDirectory ? theme_.lineNumberForeground : theme_.defaultForeground};

        const std::u32string label = BuildLabel(entries, *index, expandedDirs_);
        for (std::size_t i = 0; i < label.size() && static_cast<int>(i) < dividerColumn; ++i) {
            Cell& cell     = c[{.x = static_cast<int>(i), .y = row}];
            cell.character = text::EncodeCodepointUtf8(label[i]);
            brush.ApplyTo(cell);
        }
    }

    // Distinct brush while a drag is live -- the same "show it's grabbed"
    // feedback a real terminal/window-manager resize handle gives.
    const Brush       dividerBrush = resizing_ ? theme_.activeTab
                                               : Brush{.background = theme_.background, .foreground = theme_.lineNumberForeground};
    const std::string dividerChar  = text::EncodeCodepointUtf8(kDividerLine);
    for (int row = 0; row < c.size().height; ++row) {
        Cell& cell     = c[{.x = dividerColumn, .y = row}];
        cell.character = dividerChar;
        dividerBrush.ApplyTo(cell);
    }
}

bool ProjectSidebar::OnEvent(const Event& event) {
    if (!event.is_mouse()) {
        return false;
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

    if (mouse->button == MouseEvent::Button::WheelUp || mouse->button == MouseEvent::Button::WheelDown) {
        constexpr int kWheelScrollLines = 3;

        const std::vector<editor::ProjectTreeEntry> entries   = VisibleEntries(CachedTree());
        const int                                   maxScroll = std::max(0, static_cast<int>(entries.size()) - ContentHeight());

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

    if (mouse->at.y < kHeaderHeight) {
        // Header row -- just the project name today; consuming the click
        // here (rather than falling through to tree hit-testing below,
        // which a naive row-0 entry would otherwise resolve to) is what
        // keeps this the project-settings hook point once that exists.
        return true;
    }

    const std::vector<editor::ProjectTreeEntry> entries = VisibleEntries(CachedTree());
    if (entries.empty()) {
        return true;
    }

    const RowLayout                  layout = ComputeRowLayout(entries, scrollOffset_);
    const std::optional<std::size_t> index =
        EntryIndexAtRow(layout, entries, ContentHeight(), std::max(mouse->at.y - kHeaderHeight, 0));
    if (!index) {
        return true;
    }

    const editor::ProjectTreeEntry& entry = entries[*index];

    if (entry.isDirectory) {
        if (expandedDirs_.contains(entry.path)) {
            expandedDirs_.erase(entry.path);
        }
        else {
            expandedDirs_.insert(entry.path);
        }

        // Collapsing can shrink the visible list out from under scrollOffset_
        // -- re-filters the same (unchanged-on-disk) CachedTree() result
        // through VisibleEntries() again, now reflecting the just-toggled
        // expandedDirs_; no fresh disk walk needed, nothing on disk changed.
        const std::vector<editor::ProjectTreeEntry> after = VisibleEntries(CachedTree());
        if (!after.empty() && scrollOffset_ >= static_cast<int>(after.size())) {
            scrollOffset_ = static_cast<int>(after.size()) - 1;
        }
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    const bool isDoubleClick =
        lastFileClickPath_ && *lastFileClickPath_ == entry.path && (now - lastFileClickTime_) < kDoubleClickWindow;
    lastFileClickPath_ = entry.path;
    lastFileClickTime_ = now;

    OpenFileEntry(entry.path, isDoubleClick);
    return true;
}

void ProjectSidebar::OpenFileEntry(const std::filesystem::path& path, bool isDoubleClick) {
    try {
        if (text::Buffer* existing = bufferList_.FindByPath(path)) {
            // Re-clicking a file that's still just a temp preview (never
            // promoted) always jumps back to the top rather than carrying
            // over wherever point/scroll was left from idly looking around
            // -- a real, promoted buffer's own position is left untouched,
            // since switching back to an actively-edited file shouldn't
            // discard where you were in it.
            if (!isDoubleClick && bufferList_.PreviewBuffer() == existing) {
                existing->SetPoint(0);
            }
            activeBufferProvider_().Set(*existing);
            if (isDoubleClick && bufferList_.PreviewBuffer() == existing) {
                bufferList_.SetPreviewBuffer(nullptr); // promote the existing preview in place
            }
            statusMessage_.clear();
            return;
        }

        // A single click replaces any existing preview rather than
        // accumulating a new tab alongside it -- the old preview is only
        // ever unmodified at this point (any edit already promoted it via
        // BufferList::PreviewBuffer's own self-clearing), so discarding it
        // outright is always safe, never silent data loss.
        text::Buffer* oldPreview = !isDoubleClick ? bufferList_.PreviewBuffer() : nullptr;

        text::Buffer& opened = bufferList_.OpenOrCreateFile(path);
        activeBufferProvider_().Set(opened);
        statusMessage_.clear();
        if (!isDoubleClick) {
            bufferList_.SetPreviewBuffer(&opened);
        }

        // Closed only now, after the switch to `opened` above is fully
        // done -- closing it first (the old order) freed oldPreview's
        // Buffer right before OpenOrCreateFile's own allocation above,
        // which could reuse that exact freed address for the new buffer.
        // That made BufferView's own buffer-identity caches (topLine_,
        // syntax highlighting, ...), which still held oldPreview's address
        // from before this click, wrongly conclude nothing had changed --
        // a real bug, confirmed via a live pty run (switching from a long
        // buffer to a short one rendered as entirely blank, not merely
        // unclamped), not hypothetical.
        if (oldPreview) {
            // Must run before Close() actually frees it -- see
            // SetOnBufferClosed's own doc comment for why skipping this
            // was a real, confirmed dangling-ActiveBuffer crash.
            if (onBufferClosed_) {
                onBufferClosed_(*oldPreview);
            }
            bufferList_.Close(oldPreview->Name());
        }
    }
    catch (const std::exception& e) {
        statusMessage_ = e.what();
    }
}

bool ProjectSidebar::IsResizing() const {
    return resizing_;
}

void ProjectSidebar::BeginResize(int globalMouseX) {
    resizing_            = true;
    resizeAnchorGlobalX_ = globalMouseX;
    // Anchored to size().width (the box this widget is actually currently
    // rendered at), not the internal width_ field directly: main.cpp's
    // composition root reads Width() fresh every frame to size the box (see
    // this class's own header comment), so in real, running-editor usage
    // the two always agree by the time a drag could ever start. They can
    // disagree for exactly one frame at startup, or whenever a caller
    // constructs this widget and calls SetBox_ directly without going
    // through that same per-frame feedback loop first (every unit test) --
    // anchoring on width_ in that case silently resized from a stale value
    // instead of wherever the divider visually was, a real, confirmed bug
    // (not just a test-affecting one: the same staleness would apply to the
    // very first resize drag after startup, before Width() has been read by
    // the composition root even once).
    resizeAnchorWidth_ = size().width;
}

void ProjectSidebar::UpdateResize(int globalMouseX) {
    // Anchored to the drag's total displacement from its start, not applied
    // as a per-event delta: a move fires once per real cursor movement, and
    // once a growing drag crosses into BufferView's territory this and
    // BufferView's own OnEvent both feed the same session, so there's no
    // single, consistent "previous event" to diff against across the
    // handoff. Recomputing from the fixed start point each time sidesteps
    // that entirely.
    const int delta = globalMouseX - resizeAnchorGlobalX_;
    width_          = std::max(kMinSidebarWidth, resizeAnchorWidth_ + delta);
}

void ProjectSidebar::EndResize() {
    resizing_ = false;
}

void ProjectSidebar::RevealPath(const std::filesystem::path& targetPath) {
    std::error_code             ec;
    const std::filesystem::path absoluteTarget = std::filesystem::absolute(targetPath, ec);
    if (ec) {
        return;
    }
    const std::filesystem::path root = editor::ProjectRoot();

    // Walk upward from the target's own containing directory, collecting
    // every ancestor down to (but not including) root -- these are exactly
    // the directories VisibleEntries() needs to see in expandedDirs_ for
    // the target to actually show up, rather than being hidden behind a
    // collapsed ancestor somewhere in between.
    std::vector<std::filesystem::path> toExpand;
    std::filesystem::path              dir = absoluteTarget.parent_path();
    while (dir != root) {
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir) {
            return; // reached the filesystem root without ever finding `root` -- not under it
        }
        toExpand.push_back(dir);
        dir = parent;
    }

    for (const std::filesystem::path& ancestor : toExpand) {
        expandedDirs_.insert(ancestor);
    }
}

} // namespace ned::ui
