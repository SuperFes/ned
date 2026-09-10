#include "ProjectSidebar.h"

#include "Paint.h"
#include "ThemePaints.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <optional>
#include <system_error>

#include "Border.h"
#include "Editor/Key.h"
#include "Editor/Project/Root.h"
#include "Editor/Project/Tree.h"
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

    // changed-files-highlight follow-up: builds the absolute-path -> status
    // index RefreshVcsStatus stores. Every changed file gets its own
    // classified status; every ancestor directory between it and root gets
    // the max (most severe) status over all its changed descendants, the
    // same "does this directory contain a change" propagation VS Code's own
    // file-tree decorations do. Mirrors RevealPath's own ancestor-walk loop
    // exactly (walk parent_path() up to root, bail out at the filesystem
    // root if root is somehow never reached).
    std::unordered_map<std::filesystem::path, RowStatus> BuildVcsStatusIndex(
        const std::vector<editor::vcs::StatusEntry>& entries, const std::filesystem::path& root) {
        std::unordered_map<std::filesystem::path, RowStatus> index;
        auto                                                    merge = [&index](const std::filesystem::path& path, RowStatus status) {
            auto [it, inserted] = index.try_emplace(path, status);
            if (!inserted && status > it->second) {
                it->second = status;
            }
        };
        for (const editor::vcs::StatusEntry& entry : entries) {
            const RowStatus          status   = editor::vcs::ClassifyPorcelainStatus(entry.state);
            const std::filesystem::path filePath = (root / entry.path).lexically_normal();
            merge(filePath, status);

            std::filesystem::path dir = filePath.parent_path();
            while (dir != root) {
                const std::filesystem::path parent = dir.parent_path();
                if (parent == dir) {
                    break; // reached the filesystem root without finding root -- not under it
                }
                merge(dir, status);
                dir = parent;
            }
        }
        return index;
    }

    [[nodiscard]] RowStatus LookupVcsStatus(const std::unordered_map<std::filesystem::path, RowStatus>& index,
                                               const std::filesystem::path&                                   path) {
        const auto it = index.find(path.lexically_normal());
        return it == index.end() ? RowStatus::None : it->second;
    }

    // Reuses the diff gutter's own three constants (BufferView.cpp) rather
    // than adding new Theme fields -- foreground-only, same deliberate
    // choice Theme.h's own diffAddedBackground/diffRemovedBackground comment
    // documents (a background wash was tried for the live gutter and
    // reverted for fighting syntax-highlight contrast). BrightCyan for
    // Untracked is the one new addition, since nothing existing covers it.
    [[nodiscard]] std::optional<Color> VcsStatusColor(RowStatus status, const Theme& theme) {
        switch (status) {
            case RowStatus::Deleted:
                return theme.diagnosticError;
            case RowStatus::Modified:
                return theme.vcsModifiedForeground;
            case RowStatus::Added:
                return theme.successForeground;
            case RowStatus::Untracked:
                return theme.vcsUntrackedForeground;
            case RowStatus::None:
                return std::nullopt;
        }
        return std::nullopt;
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

void ProjectSidebar::ReturnFocus() {
    if (onFocusReturn_) {
        onFocusReturn_();
    }
}

int ProjectSidebar::ContentHeight() const {
    return std::max(0, size().height - kHeaderHeight);
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
        RefreshVcsStatus(root);
    }
    return treeCache_;
}

void ProjectSidebar::InvalidateTree() {
    treeCacheValid_ = false;
}

void ProjectSidebar::SetVcsRunner(editor::vcs::Runner* vcsRunner) {
    vcsRunner_ = vcsRunner;
}

void ProjectSidebar::DispatchVcsStatusForTesting(const std::vector<editor::vcs::StatusEntry>& entries) {
    vcsStatus_ = BuildVcsStatusIndex(entries, editor::ProjectRoot());
}

void ProjectSidebar::RefreshVcsStatus(const std::filesystem::path& root) {
    if (!vcsRunner_) {
        vcsStatus_.clear();
        return;
    }
    // A prior request still in flight (this fires at most every
    // kTreeCacheThrottle) makes RequestStatus's onError fire immediately --
    // silently discarded, vcsStatus_ just keeps its last-known contents
    // until the next tick's request actually completes. No VCS provider
    // resolving for root behaves the same way, which is exactly
    // "highlighting is only meaningful in a VCS-tracked project tree".
    vcsRunner_->RequestStatus(
        [this, root](std::vector<editor::vcs::StatusEntry> entries) { vcsStatus_ = BuildVcsStatusIndex(entries, root); },
        [](const std::string&) {});
}

void ProjectSidebar::SetOnBufferClosed(std::function<void(text::Buffer&)> handler) {
    onBufferClosed_ = std::move(handler);
}

void ProjectSidebar::SetOnBinaryFileOpenRequest(std::function<void(const std::filesystem::path&)> handler) {
    onBinaryFileOpenRequest_ = std::move(handler);
}

void ProjectSidebar::SetOnContextMenuRequest(
    std::function<void(const std::filesystem::path&, bool, Point)> handler) {
    onContextMenuRequest_ = std::move(handler);
}

void ProjectSidebar::SetOnFocusReturn(std::function<void()> handler) {
    onFocusReturn_ = std::move(handler);
}

void ProjectSidebar::SetOnHeaderClicked(std::function<void()> handler) {
    onHeaderClicked_ = std::move(handler);
}

void ProjectSidebar::Paint(Canvas c) {
    // generic-popup follow-up (Phase 3): reset the *whole* Brush here, not
    // just background_color -- an overlay (ListPopup's candidate/which-key
    // popups both start at x_min = 0, reaching into this widget's own
    // canvas) can paint a cell's foreground_color/bold/etc. while visible;
    // the Screen buffer isn't cleared between frames (only recreated on
    // resize -- Widget.h's own Screen), so any field this loop doesn't
    // touch keeps whatever a prior frame's overlay left there even once
    // it's hidden and this row goes back to being genuinely blank.
    // Confirmed live: repeated select-theme preview sessions left a
    // stale, unused foreground color baked into every blank row below the
    // tree, one popup session's leftover color replacing the last.
    //
    // Translucency follow-up (Docs/Translucency.md phase 5): the blank fill
    // is the "panel" Surface. Its derived default is exactly the flat
    // theme background this used before, so an unthemed sidebar is
    // unchanged; a theme can make it a gradient, give it an edge falloff
    // toward the buffer, or make it translucent -- in which case the dither
    // path carries whatever is behind the window through the gaps.
    const Surface panel = SurfaceFor(theme_, "panel");
    // Cleared to the theme's *own* background, not ChromeBackdrop: a
    // transparent theme means this panel shows the desktop through, and
    // painting the assumed backdrop here would make the whole sidebar
    // opaque. The chrome bars (mode line, tab strip) are the opposite case
    // -- they want a colour to fade into, and are not what the user is
    // looking through.
    ClearCanvas(c, theme_.background);
    Fill(c, panel.fill);
    for (int row = 0; row < c.size().height; ++row) {
        for (int col = 0; col < c.size().width; ++col) {
            const Point at{.x = col, .y = row};
            Cell&       cell      = c[at];
            cell.character        = " ";
            cell.foreground_color = TextColourAt(panel, c, at, theme_.defaultForeground);
        }
    }

    // unified-left-dock follow-up: row 0 is this widget's own header --
    // the live project name, click-to-switch-project (see this file's own
    // header comment for why that's an ordinary content row now rather than
    // a border title: LeftDock's own per-panel border title is a fixed
    // label, with no way to express a value that changes at runtime). Bold
    // unconditionally so the project root reads as a title rather than an
    // ordinary tree row even when unfocused; the accent tint while
    // Focused() is layered on top, the same "this has your attention"
    // signal the old border-accent frame gave -- there's no frame to tint
    // anymore, so just this row carries it.
    Brush headerBrush = Focused() ? theme_.borderAccent : theme_.tabBar;
    headerBrush.bold  = true;
    for (int col = 0; col < c.size().width; ++col) {
        headerBrush.ApplyTo(c[{.x = col, .y = 0}]);
    }
    PaintUtf8Row(c, 0, 0, ProjectNameLabel(), headerBrush, c.size().width);

    // Content fills every row below the header, full width -- no border
    // margin to inset for anymore.
    const int contentColumns = c.size().width;

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

        const std::optional<Color> vcsColor = VcsStatusColor(LookupVcsStatus(vcsStatus_, entry.path), theme_);

        // A directory is structure, so it reads *stronger* than a file, not
        // weaker: same text colour, bold. It used to take
        // lineNumberForeground -- the gutter's deliberately recessive gray --
        // which put the tree's own scaffolding below the leaves it organises
        // and left the panel looking washed out (reported live).
        //
        // A pinned ancestor is a hint about where you are, not a header bar,
        // so it takes a wash at kStickyHighlightAlpha rather than the solid
        // chrome brush it used to. Composited, so it tints whatever the
        // panel is showing -- including the desktop, for a transparent theme.
        Brush brush =
            isActiveFile ? theme_.activeTab
            : isSticky   ? Brush{.background = StickyHighlight(theme_),
                                 .foreground = vcsColor.value_or(theme_.defaultForeground),
                                 .bold       = true}
                         : Brush{.background = theme_.background,
                                 .foreground = vcsColor.value_or(theme_.defaultForeground),
                                 .bold       = entry.isDirectory};

        if (isSelected) {
            brush.background = OverlayBackground(theme_, SelectionFill(theme_));
        }
        // A row-wide background reads as a band; painting only the label's
        // own cells would leave the wash stopping mid-row.
        if (isSelected || isSticky) {
            for (int x = 0; x < contentColumns; ++x) {
                c[{.x = x, .y = row}].background_color = brush.background;
            }
        }

        const std::u32string label = BuildLabel(entries, *index, expandedDirs_);
        for (std::size_t i = 0; i < label.size() && static_cast<int>(i) < contentColumns; ++i) {
            Cell& cell     = c[{.x = static_cast<int>(i), .y = row}];
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
    const auto mouse = LocalMouseEvent(event);
    if (!mouse) {
        return false;
    }

    // click-to-focus follow-up: a real press anywhere in this widget takes
    // keyboard focus, BufferView::OnMouseEvent's own unconditional
    // TakeFocus()-at-the-top-of-the-press-handler convention -- reverses
    // this widget's original "mouse-only, clicking never steals focus"
    // design (the C-c p/focus-project-sidebar command was the only way in
    // before). Deliberately excludes wheel-scroll: scrolling to peek at the
    // tree shouldn't yank focus away from whatever you were typing.
    if (mouse->motion == MouseEvent::Motion::Pressed &&
        (mouse->button == MouseEvent::Button::Left || mouse->button == MouseEvent::Button::Right)) {
        TakeFocus();
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

    // project-sidebar-drag-drop follow-up: a release landing back on this
    // widget's own bounds is just an ordinary click (the press-time open
    // above already handled it) -- clear the armed drag with no drop
    // action. A release landing on a BufferView pane instead is handled
    // there (see that widget's own OnMouseEvent, checked ahead of its
    // LocalMouseEvent gate the same way it already checks
    // LeftDock::IsResizing()).
    if (dragPath_ && mouse->motion == MouseEvent::Motion::Released) {
        dragPath_.reset();
        return true;
    }

    // sidebar-context-menu follow-up: a right-press resolves to the same
    // row a left-press would, reports the entry's path/isDirectory plus the
    // click's absolute screen position, and stops there -- unlike a left
    // click, this never toggles a directory or opens a file; building/
    // showing the actual popup is main.cpp's job (TabBar::
    // SetOnContextMenuRequest's own shape). project-root-context-menu
    // follow-up: a right-press on the header row itself reports the
    // project root directory (isDirectory always true) instead of being
    // excluded as chrome -- main.cpp's own menu already offers New File/
    // New Folder for any directory row, so this is what lets a right-click
    // create a top-level file/directory without needing an existing entry
    // to right-click first.
    if (mouse->button == MouseEvent::Button::Right && mouse->motion == MouseEvent::Motion::Pressed) {
        if (onContextMenuRequest_ && mouse->at.y < kHeaderHeight) {
            const Box& box = Box_();
            onContextMenuRequest_(editor::ProjectRoot(), /*isDirectory=*/true,
                                  Point{.x = box.x_min + mouse->at.x, .y = box.y_min + mouse->at.y});
            return true;
        }
        if (onContextMenuRequest_ && mouse->at.y >= kHeaderHeight) {
            const std::vector<editor::ProjectTreeEntry> entries = VisibleEntries(CachedTree());
            if (!entries.empty()) {
                const RowLayout                  layout = ComputeRowLayout(entries, scrollOffset_);
                const std::optional<std::size_t> index =
                    EntryIndexAtRow(layout, entries, ContentHeight(), std::max(mouse->at.y - kHeaderHeight, 0));
                if (index) {
                    const editor::ProjectTreeEntry& entry = entries[*index];
                    selectedIndex_                        = static_cast<int>(*index);
                    const Box& box                        = Box_();
                    onContextMenuRequest_(entry.path, entry.isDirectory,
                                          Point{.x = box.x_min + mouse->at.x, .y = box.y_min + mouse->at.y});
                }
            }
        }
        return true;
    }

    if (mouse->button != MouseEvent::Button::Left || mouse->motion != MouseEvent::Motion::Pressed) {
        return false;
    }

    if (mouse->at.y < kHeaderHeight) {
        // Header row -- consuming the click here (rather than falling
        // through to tree hit-testing below, which a naive row-0 entry
        // would otherwise resolve to) is what keeps this the
        // project-settings hook point. named-projects follow-up: that hook
        // point now exists -- a click here fires switch-project the same
        // way a keybinding would. Already gated above to a real
        // left-button press, so this fires exactly once per click, no
        // separate press/release bookkeeping needed.
        if (onHeaderClicked_) {
            onHeaderClicked_();
        }
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

    // project-sidebar-drag-drop follow-up: armed alongside the existing
    // open-preview behavior below, not instead of it -- see DraggingFilePath's
    // own doc comment for the cross-widget cooperation this enables and the
    // "opens here too" side effect this deliberately accepts for a real drag.
    dragPath_ = entry.path;

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
    selectedIndex_                        = std::clamp(selectedIndex_, 0, static_cast<int>(entries.size()) - 1);
    const editor::ProjectTreeEntry& entry = entries[static_cast<std::size_t>(selectedIndex_)];

    const bool up   = chord->Special == editor::SpecialKey::Up || (chord->Control && chord->Codepoint == U'p');
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

std::optional<std::filesystem::path> ProjectSidebar::DraggingFilePath() const {
    return dragPath_;
}

void ProjectSidebar::EndFileDrag() {
    dragPath_.reset();
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
