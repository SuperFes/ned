#include "ProjectSidebar.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <optional>
#include <system_error>

#include "Border.h"
#include "Editor/Key.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectTree.h"
#include "KeyTranslation.h"
#include "Text/BinaryDetect.h"
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
    constexpr char32_t kCollapsedTriangle = U'▸';
    constexpr char32_t kExpandedTriangle  = U'▾';

    // A floor on drag-resized width, not a real design limit -- just enough
    // to keep the divider itself and a sliver of content visible so the
    // handle never becomes unreachable. There's no matching hard ceiling:
    // the containing composition's own layout naturally shrinks an
    // over-large request back down to whatever the terminal actually has.
    constexpr int kMinSidebarWidth = 4;

    // sidebar-header follow-up: row 0 is always the project-name header
    // (the title-carrying top border row since the chrome redesign), never
    // tree content -- see ProjectSidebar::Paint's own comment. The bottom
    // border row is the matching cut at the other end (ContentHeight()).
    constexpr int kHeaderHeight       = 1;
    constexpr int kBottomBorderHeight = 1;

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

    // The header's own label -- the project root's directory name (e.g.
    // opening ~/dev/ned shows "ned"), matching VS Code's own workspace-
    // sidebar-header convention, embedded into the top border edge by
    // DrawBorderTitle since the chrome redesign. filename() is empty for a
    // root path like "/" itself (no final path component to take), so this
    // falls back to the full path string in that case rather than showing a
    // blank header.
    std::string ProjectNameLabel() {
        const std::filesystem::path root     = editor::ProjectRoot();
        const std::string           filename = root.filename().string();
        return filename.empty() ? root.string() : filename;
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
    // Collapsed reports the 1-column strip; width_ itself is preserved so
    // expanding restores the previous width exactly (see Collapsed()).
    return collapsed_ ? 1 : width_;
}

void ProjectSidebar::SetWidth(int width) {
    width_ = std::max(kMinSidebarWidth, width);
}

bool ProjectSidebar::Collapsed() const {
    return collapsed_;
}

void ProjectSidebar::SetCollapsed(bool collapsed) {
    collapsed_ = collapsed;
    if (collapsed_ && resizing_) {
        EndResize(); // a resize session can't meaningfully outlive the frame it was resizing
    }
    if (collapsed_ && Focused() && onFocusReturn_) {
        // Collapsing a keyboard-focused sidebar (C-c C-p while inside it)
        // would leave the keyboard captured by a 1-column strip -- hand
        // focus back to the editor instead. An explicit collapse already
        // lands on the state a pending TakeKeyboardFocus restore would
        // recreate, so the flag is spent here too.
        collapseOnFocusReturn_ = false;
        onFocusReturn_();
    }
}

void ProjectSidebar::TakeKeyboardFocus() {
    collapseOnFocusReturn_ = collapsed_;
    SetCollapsed(false);
    TakeFocus();
}

void ProjectSidebar::ReturnFocus() {
    // Hand focus back *before* re-collapsing: once Focused() is false,
    // SetCollapsed(true)'s own captured-keyboard branch (above) can't fire
    // onFocusReturn_ a second time.
    const bool recollapse  = collapseOnFocusReturn_;
    collapseOnFocusReturn_ = false;
    if (onFocusReturn_) {
        onFocusReturn_();
    }
    if (recollapse) {
        SetCollapsed(true);
    }
}

void ProjectSidebar::ToggleCollapsed() {
    CommitCollapsed(!collapsed_);
}

void ProjectSidebar::CommitCollapsed(bool collapsed) {
    SetCollapsed(collapsed);
    if (onCollapseCommitted_) {
        onCollapseCommitted_(collapsed_);
    }
}

int ProjectSidebar::ExpandedWidth() const {
    return width_;
}

int ProjectSidebar::ContentHeight() const {
    return std::max(0, size().height - kHeaderHeight - kBottomBorderHeight);
}

const std::vector<editor::ProjectTreeEntry>& ProjectSidebar::CachedTree() {
    const std::filesystem::path root = editor::ProjectRoot();
    const auto                  now  = std::chrono::steady_clock::now();
    if (!treeCacheValid_ || root != treeCacheRoot_ || (now - treeCacheTime_) >= kTreeCacheThrottle) {
        treeCache_ = editor::BuildProjectTree(
            root, [this](const std::filesystem::path& dir) { return expandedDirs_.contains(dir); });
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

void ProjectSidebar::SetOnBinaryFileOpenRequest(std::function<void(const std::filesystem::path&)> handler) {
    onBinaryFileOpenRequest_ = std::move(handler);
}

void ProjectSidebar::SetOnFocusReturn(std::function<void()> handler) {
    onFocusReturn_ = std::move(handler);
}

void ProjectSidebar::SetOnWidthCommitted(std::function<void(int)> handler) {
    onWidthCommitted_ = std::move(handler);
}

void ProjectSidebar::SetOnCollapseCommitted(std::function<void(bool)> handler) {
    onCollapseCommitted_ = std::move(handler);
}

void ProjectSidebar::Paint(Canvas c) {
    for (int row = 0; row < c.size().height; ++row) {
        for (int col = 0; col < c.size().width; ++col) {
            Cell& cell            = c[{.x = col, .y = row}];
            cell.character        = " ";
            cell.background_color = theme_.background;
        }
    }

    // Collapsed (chrome-redesign follow-up): a single border-column strip
    // with an accent hint glyph on the header row -- the always-visible
    // mouse affordance that replaced the separate SidebarToggle widget
    // (double-click expands; see OnEvent).
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

    // The rounded frame (chrome-redesign follow-up): row 0 carries the
    // project name as the header title (sidebar-header follow-up --
    // main.cpp's composition gives ProjectSidebar the rows tabBar used to
    // span, so this top edge sits beside the tab-label row), the right
    // border column doubles as the resize divider, and the whole frame
    // takes the accent brush while a drag is live -- the same "show it's
    // grabbed" feedback a real window-manager resize handle gives -- or
    // while this widget holds the keyboard focus (sidebar-keyboard-focus
    // follow-up), the same accent-frame signal doing double duty.
    const Brush frameBrush = (resizing_ || Focused()) ? theme_.borderAccent : theme_.border;
    DrawBorder(c, frameBrush);
    DrawBorderTitle(c, ProjectNameLabel(), theme_.borderAccent);

    // Content renders inside the frame: rows [kHeaderHeight,
    // height - 1 - kBottomBorderHeight], columns [1, width - 2].
    const int contentLeft     = 1;
    const int contentColumns  = std::max(0, c.size().width - 2);

    const std::vector<editor::ProjectTreeEntry> entries    = VisibleEntries(CachedTree());
    const std::optional<std::filesystem::path>& activePath = activeBufferProvider_().Get().Path();

    const int       contentHeight = ContentHeight();
    const RowLayout layout        = ComputeRowLayout(entries, scrollOffset_);
    const int       stickyCount   = std::min<int>(static_cast<int>(layout.stickyAncestors.size()), contentHeight);

    // Keep the keyboard selection cursor pointing at a real entry across
    // tree changes (sidebar-keyboard-focus follow-up).
    if (!entries.empty()) {
        selectedIndex_ = std::clamp(selectedIndex_, 0, static_cast<int>(entries.size()) - 1);
    }
    const bool focused = Focused();

    for (int contentRow = 0; contentRow < contentHeight; ++contentRow) {
        const std::optional<std::size_t> index = EntryIndexAtRow(layout, entries, contentHeight, contentRow);
        if (!index) {
            break; // sticky rows always come first; once content runs out, so do all later rows
        }
        const editor::ProjectTreeEntry& entry    = entries[*index];
        const bool                      isSticky = contentRow < stickyCount;
        const int                       row      = contentRow + kHeaderHeight;

        const bool isActiveFile = !entry.isDirectory && activePath && *activePath == entry.path;
        // The keyboard selection cursor, only meaningful while focused --
        // reuses the buffer's own selection overlay color so "selected"
        // reads the same everywhere.
        const bool isSelected = focused && static_cast<int>(*index) == selectedIndex_;

        Brush brush =
            isActiveFile ? theme_.activeTab
            : isSticky   ? theme_.tabBar // pinned ancestor header -- same chrome family as TabBar's own row
                         : Brush{.background = theme_.background,
                                 .foreground = entry.isDirectory ? theme_.lineNumberForeground : theme_.defaultForeground};
        if (isSelected) {
            brush.background = theme_.selectionBackground;
            for (int x = contentLeft; x < contentLeft + contentColumns; ++x) {
                c[{.x = x, .y = row}].background_color = theme_.selectionBackground;
            }
        }

        const std::u32string label = BuildLabel(entries, *index, expandedDirs_);
        for (std::size_t i = 0; i < label.size() && static_cast<int>(i) < contentColumns; ++i) {
            Cell& cell     = c[{.x = contentLeft + static_cast<int>(i), .y = row}];
            cell.character = text::EncodeCodepointUtf8(label[i]);
            brush.ApplyTo(cell);
        }
    }
}

bool ProjectSidebar::OnEvent(const Event& event) {
    if (!event.is_mouse()) {
        // sidebar-keyboard-focus follow-up: key events only ever reach a
        // widget via the focus registry (main.cpp routes them straight to
        // FocusedWidget()), so anything arriving here while unfocused is
        // not ours to handle.
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

    // Collapsed (chrome-redesign follow-up): the whole 1-column strip is
    // the divider -- a double-press expands, anything else is consumed but
    // inert (there's no content to scroll or click).
    if (collapsed_) {
        if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
            const auto now = std::chrono::steady_clock::now();
            if (dividerClickPending_ && (now - lastDividerPressTime_) < kDoubleClickWindow) {
                dividerClickPending_ = false;
                CommitCollapsed(false);
            }
            else {
                dividerClickPending_  = true;
                lastDividerPressTime_ = now;
            }
        }
        return true;
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
        // The right border column is the divider: a second press within the
        // double-click window collapses (the just-started resize session
        // from the first press dies with it via SetCollapsed); a single
        // press starts a resize session as always. A real drag clears the
        // pending double-click -- see UpdateResize.
        const auto now = std::chrono::steady_clock::now();
        if (dividerClickPending_ && (now - lastDividerPressTime_) < kDoubleClickWindow) {
            dividerClickPending_ = false;
            CommitCollapsed(true);
            return true;
        }
        dividerClickPending_  = true;
        lastDividerPressTime_ = now;
        BeginResize(rawMouse.at.x);
        return true;
    }

    if (mouse->at.y < kHeaderHeight) {
        // Header row (the title-carrying top border) -- consuming the click
        // here (rather than falling through to tree hit-testing below,
        // which a naive row-0 entry would otherwise resolve to) is what
        // keeps this the project-settings hook point once that exists.
        return true;
    }

    if (mouse->at.y >= size().height - kBottomBorderHeight) {
        return true; // bottom border row -- chrome, not content
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

    // Keep the keyboard selection cursor on whatever the mouse last touched
    // (sidebar-keyboard-focus follow-up), so focusing the sidebar afterward
    // starts from there rather than an unrelated stale row.
    selectedIndex_ = static_cast<int>(*index);

    if (entry.isDirectory) {
        ToggleDirectory(entry.path);
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
    catch (const text::BinaryFileError&) {
        if (onBinaryFileOpenRequest_) {
            onBinaryFileOpenRequest_(path);
        }
        else {
            statusMessage_ = "\"" + path.string() + "\" looks like a binary file.";
        }
    }
    catch (const std::exception& e) {
        statusMessage_ = e.what();
    }
}

void ProjectSidebar::ToggleDirectory(const std::filesystem::path& path) {
    if (expandedDirs_.contains(path)) {
        expandedDirs_.erase(path);
    }
    else {
        expandedDirs_.insert(path);
    }

    // project-sidebar-eager-walk follow-up: CachedTree() itself now
    // prunes unexpanded subtrees at walk time (not just VisibleEntries'
    // display-time filtering), so expanding a directory for the first
    // time means its children were never actually walked onto disk yet
    // -- InvalidateTree() forces CachedTree() to rebuild right away
    // rather than waiting out kTreeCacheThrottle, cheap here since a
    // click/keypress is a rare, one-off event and the rebuilt walk only
    // descends into whatever's newly expanded, not the whole tree.
    InvalidateTree();
    const std::vector<editor::ProjectTreeEntry> after = VisibleEntries(CachedTree());
    if (!after.empty() && scrollOffset_ >= static_cast<int>(after.size())) {
        scrollOffset_ = static_cast<int>(after.size()) - 1;
    }
}

void ProjectSidebar::EnsureSelectionVisible() {
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

bool ProjectSidebar::HandleKeyEvent(const Event& event) {
    const auto chord = TranslateKey(event);
    if (!chord) {
        return true; // focused: swallow undecodable input rather than leaking it
    }

    const bool cancel = chord->Special == editor::SpecialKey::Escape ||
                        (chord->Control && chord->Codepoint == U'g');

    const std::vector<editor::ProjectTreeEntry> entries = VisibleEntries(CachedTree());
    if (entries.empty()) {
        if (cancel) {
            ReturnFocus();
        }
        return true; // nothing to navigate; still consume -- we hold focus
    }
    selectedIndex_ = std::clamp(selectedIndex_, 0, static_cast<int>(entries.size()) - 1);
    const editor::ProjectTreeEntry& entry = entries[static_cast<std::size_t>(selectedIndex_)];

    const bool up = chord->Special == editor::SpecialKey::Up || (chord->Control && chord->Codepoint == U'p');
    const bool down = chord->Special == editor::SpecialKey::Down || (chord->Control && chord->Codepoint == U'n');

    if (up || down) {
        selectedIndex_ = std::clamp(selectedIndex_ + (down ? 1 : -1), 0, static_cast<int>(entries.size()) - 1);
        EnsureSelectionVisible();
        return true;
    }
    if (chord->Special == editor::SpecialKey::Enter) {
        if (entry.isDirectory) {
            ToggleDirectory(entry.path);
        }
        else {
            // A deliberate keyboard open is a permanent one (the same
            // promotion a mouse double-click gets, never a transient
            // preview), and it hands focus straight back to the editor --
            // the point of opening a file is to edit it.
            OpenFileEntry(entry.path, /*isDoubleClick=*/true);
            ReturnFocus();
        }
        return true;
    }
    if (chord->Special == editor::SpecialKey::Right) {
        if (entry.isDirectory && !expandedDirs_.contains(entry.path)) {
            ToggleDirectory(entry.path);
        }
        return true;
    }
    if (chord->Special == editor::SpecialKey::Left) {
        if (entry.isDirectory && expandedDirs_.contains(entry.path)) {
            ToggleDirectory(entry.path);
        }
        return true;
    }
    if (cancel) {
        ReturnFocus();
        return true;
    }
    return true; // every other key is consumed while this widget holds focus
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
    resizeStartWidth_  = width_;
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
    if (delta < -1 || delta > 1) {
        // A real drag, not a slightly-wobbly click -- stop it counting as
        // the first half of a collapse double-click (see OnEvent).
        dividerClickPending_ = false;
    }
}

void ProjectSidebar::EndResize() {
    if (!resizing_) {
        return;
    }
    resizing_ = false;
    // sidebar-width-memory follow-up: a committed drag that actually moved
    // the divider reports the new width; main.cpp wires this to
    // editor::SetVariable so it persists as the global default width for
    // future runs (a project session's own stored width still wins at
    // startup). A divider click that never moved reports nothing, and the
    // hook stays a policy-free callback so unit-test drags never touch the
    // real variables.json.
    if (width_ != resizeStartWidth_ && onWidthCommitted_) {
        onWidthCommitted_(width_);
    }
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

    // project-sidebar-eager-walk follow-up: same reasoning as the click
    // handler above -- newly-expanded ancestors' children were never
    // walked onto disk, since CachedTree() now prunes unexpanded subtrees
    // at walk time. A no-op cost-wise on the actual startup call site
    // (treeCacheValid_ is still false at that point anyway), but keeps
    // this correct if RevealPath is ever called after the first paint.
    InvalidateTree();
}

} // namespace ned::ui
