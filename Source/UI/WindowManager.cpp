#include "WindowManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

#include "Editor/AutoMerge.h"
#include "Editor/AutoRevert.h"
#include "Editor/Backup.h"
#include "Editor/Bookmark.h"
#include "Editor/Dap/DapManager.h"
#include "Editor/DiagnosticsLog.h"
#include "Editor/Lsp/LspBackgroundSync.h"
#include "Editor/MinimapSettings.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Multibuffer.h"
#include "Editor/PersistentUndo.h"
#include "Editor/ProjectSession.h"
#include "Editor/RecentFiles.h"
#include "Editor/ScratchPad.h"
#include "Editor/Session.h"
#include "Editor/TabWidth.h"

namespace ned::ui {

namespace {

    // See WindowManager::StartAutoSaveTimer's own header comment for why
    // this moved here, verbatim, from BufferView.
    constexpr std::chrono::milliseconds kScratchAutoSaveInterval{5000};

    // The one-column/one-row divider between a split's two children --
    // chrome-redesign follow-up: drawn with the theme's border brush so
    // split dividers, the sidebar frame, and the tab underline all read as
    // one line family. Split-resize follow-up: doubles as the mouse-drag
    // resize handle for both split kinds (a SplitBelow previously drew no
    // divider at all -- the top pane's own ModeLine row stood in as the
    // visual boundary -- but that row belongs to the Pane, not this WindowNode,
    // giving nothing here to grab; a real, WindowNode-owned row is what
    // gives drag-resize a stable place to live, symmetric with SplitRight's
    // own column), and takes the accent brush while a drag is live, same
    // "show it's grabbed" signal ProjectSidebar's own resize divider already
    // gives.
    //
    // Owns its own drag state exactly like ProjectSidebar
    // (BeginResize/UpdateResize/EndResize, IsResizing()) -- node is the
    // WindowNode this divider belongs to (stable: a WindowNode is always
    // heap-allocated via unique_ptr and never moved once built, the same
    // invariant Pane itself relies on), mutated directly since
    // BuildComponent's DynamicFixed SizeSpec for `first` reads node.ratio
    // fresh every Paint() -- no rebuild needed mid-drag. onResizingChanged
    // is how WindowManager's own resizingSplit_ flag (consulted by every
    // pane's BufferView, see WindowManager.h's own comment) tracks whether
    // *this* divider is the one currently live.
    class SplitDivider : public Widget {
      public:
        SplitDivider(const Theme& theme, bool vertical, WindowNode& node, std::function<void(bool)> onResizingChanged) : theme_(theme), vertical_(vertical), node_(node), onResizingChanged_(std::move(onResizingChanged)) {
        }

        void Paint(Canvas c) override {
            const Brush& brush = resizing_ ? theme_.borderAccent : theme_.border;
            if (vertical_) {
                for (int y = 0; y < c.size().height; ++y) {
                    Cell& cell     = c[{.x = 0, .y = y}];
                    cell.character = "│"; // BOX DRAWINGS LIGHT VERTICAL
                    brush.ApplyTo(cell);
                }
            }
            else {
                for (int x = 0; x < c.size().width; ++x) {
                    Cell& cell     = c[{.x = x, .y = 0}];
                    cell.character = "─"; // BOX DRAWINGS LIGHT HORIZONTAL
                    brush.ApplyTo(cell);
                }
            }
        }

        bool OnEvent(const Event& event) override {
            if (!event.is_mouse()) {
                return false;
            }
            const MouseEvent rawMouse    = event.mouse();
            const int        globalCoord = vertical_ ? rawMouse.at.x : rawMouse.at.y;

            // Checked first, regardless of position -- same "every leaf gets
            // every event, a live drag session owns move/release outright"
            // contract ProjectSidebar's own IsResizing()-gated handling in
            // BufferView relies on (see Widget.h's own header comment).
            if (resizing_) {
                if (rawMouse.motion == MouseEvent::Motion::Moved) {
                    UpdateResize(globalCoord);
                    return true;
                }
                if (rawMouse.motion == MouseEvent::Motion::Released) {
                    EndResize();
                    return true;
                }
            }

            const auto mouse = LocalMouseEvent(event);
            if (!mouse) {
                return false;
            }
            if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
                BeginResize(globalCoord);
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsResizing() const {
            return resizing_;
        }

      private:
        void BeginResize(int globalCoord) {
            resizing_          = true;
            anchorGlobalCoord_ = globalCoord;
            anchorRatio_       = node_.ratio;
            if (onResizingChanged_) {
                onResizingChanged_(true);
            }
        }

        // Anchored to the drag's total displacement from its start (same
        // shape as ProjectSidebar::UpdateResize's own doc comment on why --
        // a growing drag delivers move events from BufferView's own
        // OnMouseEvent once it strays past this divider's own thin box, so
        // this must keep working from globalCoord alone, not anything
        // relative to a Box_() this call may not even be inside anymore).
        // node_.container's own Box_() was already set by its parent's
        // LayoutChildren before this frame's Paint() reached it -- valid to
        // read here regardless of whether a Paint has happened *this* frame
        // yet, since a mouse-driven Moved event only ever follows at least
        // one prior full frame.
        void UpdateResize(int globalCoord) {
            if (!node_.container) {
                return;
            }
            const Box&  box        = node_.container->Box_();
            const int   totalLen   = vertical_ ? (box.x_max - box.x_min + 1) : (box.y_max - box.y_min + 1);
            const int   available  = std::max(1, totalLen - 1); // minus this divider's own 1-cell thickness
            const int   delta      = globalCoord - anchorGlobalCoord_;
            const float ratioDelta = static_cast<float>(delta) / static_cast<float>(available);

            // A minimum-pixel floor (not a fixed ratio floor) so a resize
            // never shrinks either side below something still usable,
            // adaptively scaled to whatever room actually exists -- same
            // reasoning as ProjectSidebar's own kMinSidebarWidth, expressed
            // as a ratio since that's what's actually stored here.
            constexpr int kMinPanePixels = 4;
            const float   minRatio       = std::min(0.5f, static_cast<float>(kMinPanePixels) / static_cast<float>(std::max(1, totalLen)));
            const float   maxRatio       = 1.0f - minRatio;

            node_.ratio = std::clamp(anchorRatio_ + ratioDelta, minRatio, maxRatio);
        }

        void EndResize() {
            resizing_ = false;
            if (onResizingChanged_) {
                onResizingChanged_(false);
            }
        }

        const Theme&              theme_;
        bool                      vertical_; // true: SplitRight's column divider; false: SplitBelow's row divider
        WindowNode&               node_;
        std::function<void(bool)> onResizingChanged_;

        bool  resizing_          = false;
        int   anchorGlobalCoord_ = 0;
        float anchorRatio_       = 0.5f;
    };

} // namespace

Pane::Pane(text::Buffer& buffer, text::KillRing& killRing, editor::RegisterTable& registers,
           editor::PromptHistory& promptHistory, text::BufferList& bufferList, const editor::CommandRegistry& registry,
           const editor::Keymap& janetKeymap, const editor::Keymap& globalKeymap, editor::Mode mode,
           std::string& statusMessage, const Theme& theme, ProjectSidebar* projectSidebar,
           editor::lsp::LspManager* lspManager, editor::tasks::TaskRunner* taskRunner,
           editor::testrun::TestRunner* testRunner, editor::vcs::VcsRunner* vcsRunner, editor::dap::DapManager* dapManager,
           editor::acp::AcpManager* acpManager, editor::ProjectUndoManager* projectUndo, const janet::Environment* janetEnv,
           std::function<void(editor::InteractiveRequest)> onWindowRequest,
           std::function<void(text::Buffer&)>              onBufferClosed) : activeBuffer_(buffer), mode_(std::move(mode)),
                                                                dispatcher_(registry, editor::KeymapStack({&janetKeymap, &mode_.keymap, &globalKeymap})),
                                                                bufferView_(std::make_shared<BufferView>(activeBuffer_, killRing, registers, promptHistory, bufferList, dispatcher_,
                                                                                                         statusMessage, mode_, theme)),
                                                                modeLine_(std::make_shared<ModeLine>(activeBuffer_, mode_, theme)),
                                                                scrollBar_(std::make_shared<ScrollBar>(theme.scrollBar)),
                                                                scrollUp_(std::make_shared<ScrollArrowButton>(U'▲', theme.scrollBar, theme.scrollBarDisabled)),
                                                                scrollDown_(std::make_shared<ScrollArrowButton>(U'▼', theme.scrollBar, theme.scrollBarDisabled)),
                                                                minimap_(std::make_shared<Minimap>(activeBuffer_, mode_, theme)),
                                                                scrollColumn_(Axis::Vertical,
                                                                              {
                                                                                  {scrollUp_.get(), SizeSpec::Fixed(1)},
                                                                                  {scrollBar_.get(), SizeSpec::Flex()},
                                                                                  {scrollDown_.get(), SizeSpec::Fixed(1)},
                                                                              }),
                                                                row_(Axis::Horizontal,
                                                                     {
                                                                         {bufferView_.get(), SizeSpec::Flex()},
                                                                         {&scrollColumn_, SizeSpec::Fixed(1)},
                                                                         {minimap_.get(), SizeSpec::DynamicFixed([] { return editor::MinimapWidth(); })},
                                                                     }),
                                                                component_(Axis::Vertical,
                                                                           {
                                                                               {&row_, SizeSpec::Flex()},
                                                                               {modeLine_.get(), SizeSpec::Fixed(1)},
                                                                           }) {
    bufferView_->SetScrollBar(scrollBar_.get());
    bufferView_->SetScrollArrows(scrollUp_.get(), scrollDown_.get());
    // MRU-close follow-up: every buffer switch in this pane (tab click,
    // find-file, switch-to-buffer, sidebar preview, close reassignment)
    // funnels through activeBuffer_.Set -- record it, so CloseBufferNow can
    // land on the most recently left buffer. bufferList outlives every Pane
    // (shared app-wide, per this constructor's own doc comment). The
    // initial buffer is touched directly: Set never fired for it.
    // editor-ergonomics follow-up: piggybacked on this same choke point --
    // find-recent-file's candidate list is "every path-backed buffer this
    // pane has ever switched to," not just ones opened via find-file/
    // project-find-file directly.
    activeBuffer_.SetOnChange([&bufferList](text::Buffer& current) {
        bufferList.TouchBuffer(current);
        editor::RecordRecentFile(current);
    });
    bufferList.TouchBuffer(buffer);
    editor::RecordRecentFile(buffer);
    // Chrome-redesign follow-up: this pane's mode line accent-tints its
    // gradient while this pane's own BufferView holds the keyboard focus --
    // the raw pointer outlives modeLine_ (both are members of this Pane).
    modeLine_->SetFocusProvider([view = bufferView_.get()] { return view->Focused(); });
    // embedded-language-documents follow-up: same raw-pointer-outlives
    // reasoning as SetFocusProvider just above -- shows which embedded
    // language (if any) governs this pane's buffer at its current point.
    modeLine_->SetLanguageAtPointProvider([view = bufferView_.get()] { return view->EmbeddedLanguageAtPoint(); });
    bufferView_->SetMinimap(minimap_.get(), &scrollColumn_);
    // Minimap widget follow-up: exactly one of the two ever occupies row_'s
    // trailing column -- seeded here from the process-wide setting, kept in
    // lockstep opposition afterward by toggle-minimap (see
    // BufferView::SetMinimap's own doc comment).
    minimap_->active     = editor::MinimapEnabled();
    scrollColumn_.active = !minimap_->active;
    bufferView_->SetProjectSidebar(projectSidebar);
    bufferView_->SetLspManager(lspManager);
    modeLine_->SetLspManager(lspManager);
    bufferView_->SetTaskRunner(taskRunner);
    bufferView_->SetTestRunner(testRunner);
    bufferView_->SetVcsRunner(vcsRunner);
    bufferView_->SetDapManager(dapManager);
    bufferView_->SetAcpManager(acpManager);
    bufferView_->SetProjectUndo(projectUndo);
    // user-facing-hang-affordance follow-up: every real, composed pane opts
    // into the live *Messages* alert -- see BufferView::
    // SetSurfaceUnseenLogEntries's own doc comment for why this is opt-in
    // rather than always-on.
    bufferView_->SetSurfaceUnseenLogEntries(true);
    bufferView_->SetJanetEnvironment(janetEnv);
    bufferView_->SetOnWindowRequest(std::move(onWindowRequest));
    bufferView_->SetOnBufferClosed(std::move(onBufferClosed));
    // per-buffer-mode follow-up: Mode is a property of the buffer being
    // viewed, not this pane -- reassigning mode_ in place is sufficient to
    // swap highlighting/folding/keymap/expand-selection all at once, since
    // dispatcher_'s KeymapStack above already points at &mode_.keymap (the
    // member's own stable address), not a snapshot taken at construction.
    bufferView_->SetOnActiveBufferChanged([this](text::Buffer& changedBuffer) { mode_ = editor::CachedModeForBuffer(changedBuffer); });

    scrollBar_->SetOnScroll(
        [this](int position) { bufferView_->SetTopLine(static_cast<std::size_t>(position)); });
    scrollUp_->SetOnClick([this] {
        const std::size_t top = bufferView_->TopLine();
        bufferView_->SetTopLine(top > 0 ? top - 1 : 0);
    });
    scrollDown_->SetOnClick([this] { bufferView_->SetTopLine(bufferView_->TopLine() + 1); });
    minimap_->SetOnScroll(
        [this](int position) { bufferView_->SetTopLine(static_cast<std::size_t>(position)); });
}

ActiveBuffer& Pane::ActiveBufferRef() {
    return activeBuffer_;
}

bool Pane::MinimapActive() const {
    return minimap_->active;
}

void Pane::ReleaseMinimapPixelPlane() {
    minimap_->ReleasePlane();
}

void Pane::ClearBufferCaches(text::Buffer& buffer) {
    bufferView_->ClearBufferCaches(buffer);
    minimap_->ClearBufferCache(buffer);
}

bool Pane::ScrollColumnActive() const {
    return scrollColumn_.active;
}

BufferView& Pane::Buffer() {
    return *bufferView_;
}

ModeLine& Pane::ModeLineRef() {
    return *modeLine_;
}

const editor::Mode& Pane::ModeRef() const {
    return mode_;
}

Widget& Pane::Component() {
    return component_;
}

void Pane::SetEventLoop(EventLoop* eventLoop) {
    bufferView_->SetEventLoop(eventLoop);
    scrollUp_->SetEventLoop(eventLoop);
    scrollDown_->SetEventLoop(eventLoop);
    minimap_->SetEventLoop(eventLoop);
}

namespace {

    void CollectLeaves(WindowNode* node, std::vector<Pane*>& out) {
        if (node == nullptr) {
            return;
        }
        if (node->kind == WindowNode::Kind::Leaf) {
            out.push_back(node->pane.get());
            return;
        }
        CollectLeaves(node->first.get(), out);
        CollectLeaves(node->second.get(), out);
    }

    Pane* FirstLeaf(WindowNode* node) {
        if (node->kind == WindowNode::Kind::Leaf) {
            return node->pane.get();
        }
        return FirstLeaf(node->first.get());
    }

    // Finds the Split node whose direct child is the Leaf holding `target`,
    // and returns the *other* child's first leaf -- i.e. "the pane that
    // will visually take over target's space once target is deleted."
    Pane* FindSiblingFirstLeaf(WindowNode* node, Pane* target) {
        if (node->kind == WindowNode::Kind::Leaf) {
            return nullptr;
        }
        if (node->first->kind == WindowNode::Kind::Leaf && node->first->pane.get() == target) {
            return FirstLeaf(node->second.get());
        }
        if (node->second->kind == WindowNode::Kind::Leaf && node->second->pane.get() == target) {
            return FirstLeaf(node->first.get());
        }
        if (Pane* found = FindSiblingFirstLeaf(node->first.get(), target)) {
            return found;
        }
        return FindSiblingFirstLeaf(node->second.get(), target);
    }

    // Replaces the Leaf holding `target`, in place within whichever slot
    // currently owns it (root_ itself, or a parent's first/second), with a
    // new Split node containing the original leaf and a freshly-built one.
    // Returns true once handled -- a plain recursive tree edit, no parent
    // pointers needed anywhere in WindowNode.
    //
    // newPane is taken by reference, not by value, and deliberately not
    // std::move'd at either recursive call site below -- a real, confirmed
    // bug (found via a live coredump backtrace, not guessed) existed here
    // when it was taken by value: passing std::move(newPane) into the
    // node->first recursive call unconditionally leaves the *caller's*
    // newPane moved-from once that call returns, regardless of whether
    // node->first's own subtree actually contained target -- so whenever
    // target lives anywhere other than the very first leaf in tree order,
    // the node->second call below received an already-null newPane, and the
    // freshly-made Pane got silently destroyed instead of ever reaching the
    // matching leaf. The leaf that *did* match target then got a
    // WindowNode{kind = Leaf, pane = nullptr} spliced into the live tree,
    // which crashed the very next RebuildComponentTree() call the instant
    // BuildComponent tried node->pane->Component() on it. Taking newPane by
    // reference fixes this the same way: it's only ever actually moved out
    // at the one successful-match branch below, so an unsuccessful sibling
    // search leaves it untouched for whichever call finds the real match.
    bool SplitLeafInTree(std::unique_ptr<WindowNode>& node, Pane* target, WindowNode::Kind splitKind,
                         std::unique_ptr<Pane>& newPane) {
        if (node->kind == WindowNode::Kind::Leaf) {
            if (node->pane.get() != target) {
                return false;
            }
            auto originalLeaf = std::move(node);

            auto newLeaf  = std::make_unique<WindowNode>();
            newLeaf->kind = WindowNode::Kind::Leaf;
            newLeaf->pane = std::move(newPane);

            auto split  = std::make_unique<WindowNode>();
            split->kind = splitKind;
            // The original pane keeps its place as `first` -- Emacs' own
            // split-window-below/-right always puts the *new* window second
            // (below/right of the one being split).
            split->first  = std::move(originalLeaf);
            split->second = std::move(newLeaf);

            node = std::move(split);
            return true;
        }
        if (SplitLeafInTree(node->first, target, splitKind, newPane)) {
            return true;
        }
        return SplitLeafInTree(node->second, target, splitKind, newPane);
    }

    // Replaces the Split node whose direct child is the Leaf holding
    // `target` with whichever sibling child survives. Returns true once
    // handled. Never called with a `node` that's itself the sole Leaf in
    // the whole tree -- callers check that case first (see DeleteWindow).
    bool DeleteLeafInTree(std::unique_ptr<WindowNode>& node, Pane* target) {
        if (node->kind == WindowNode::Kind::Leaf) {
            return false; // handled one level up, by our own parent
        }
        if (node->first->kind == WindowNode::Kind::Leaf && node->first->pane.get() == target) {
            node = std::move(node->second);
            return true;
        }
        if (node->second->kind == WindowNode::Kind::Leaf && node->second->pane.get() == target) {
            node = std::move(node->first);
            return true;
        }
        if (DeleteLeafInTree(node->first, target)) {
            return true;
        }
        return DeleteLeafInTree(node->second, target);
    }

    // Moves the Pane holding `target` out of the tree entirely, discarding
    // everything else in the subtree it's searched from -- used by
    // DeleteOtherWindows, where every other pane really is being closed.
    std::unique_ptr<Pane> ExtractPane(std::unique_ptr<WindowNode>& node, Pane* target) {
        if (node->kind == WindowNode::Kind::Leaf) {
            if (node->pane.get() == target) {
                return std::move(node->pane);
            }
            return nullptr;
        }
        if (auto found = ExtractPane(node->first, target)) {
            return found;
        }
        return ExtractPane(node->second, target);
    }

    bool ContainsPane(const WindowNode* node, const Pane* target) {
        if (node->kind == WindowNode::Kind::Leaf) {
            return node->pane.get() == target;
        }
        return ContainsPane(node->first.get(), target) || ContainsPane(node->second.get(), target);
    }

    // Split-resize follow-up: the *nearest* ancestor of `target` (closest to
    // the leaf, not the root) whose own kind matches axisKind -- i.e. the
    // one split boundary that actually sits directly against target's own
    // pane along that axis. Recurses into whichever child contains target
    // first, so a deeper match always wins over a shallower one along the
    // same path. targetInFirst tells the caller which side of that split
    // target is on (grow means "make that side bigger").
    struct SplitAncestor {
        WindowNode* node;
        bool        targetInFirst;
    };

    std::optional<SplitAncestor> FindNearestSplitAncestor(WindowNode* node, const Pane* target, WindowNode::Kind axisKind) {
        if (node->kind == WindowNode::Kind::Leaf) {
            return std::nullopt;
        }
        const bool  targetInFirst   = ContainsPane(node->first.get(), target);
        WindowNode* childWithTarget = targetInFirst ? node->first.get() : node->second.get();
        if (auto deeper = FindNearestSplitAncestor(childWithTarget, target, axisKind)) {
            return deeper;
        }
        if (node->kind == axisKind) {
            return SplitAncestor{node, targetInFirst};
        }
        return std::nullopt;
    }

} // namespace

WindowManager::WindowManager(text::Buffer& initialBuffer, text::KillRing& killRing, editor::RegisterTable& registers,
                             editor::PromptHistory& promptHistory, text::BufferList& bufferList,
                             const editor::CommandRegistry& registry, const editor::Keymap& janetKeymap,
                             const editor::Keymap& globalKeymap, editor::Mode initialMode, std::string& statusMessage,
                             const Theme& theme) : killRing_(killRing), registers_(registers), promptHistory_(promptHistory), bufferList_(bufferList), registry_(registry), janetKeymap_(janetKeymap),
                                                   globalKeymap_(globalKeymap), statusMessage_(statusMessage), theme_(theme) {
    root_       = std::make_unique<WindowNode>();
    root_->kind = WindowNode::Kind::Leaf;
    root_->pane = MakePane(initialBuffer, std::move(initialMode));

    RebuildComponentTree();

    // Deliberately NOT calling root_->pane->Buffer().TakeFocus() here --
    // see this class's own public TakeFocus()'s doc comment (WindowManager.h)
    // for why that would be a real, confirmed-by-testing no-op at this
    // point (rootComponent_ has no parent yet) and main.cpp must call the
    // public version again once RootComponent() is actually embedded into
    // the app's full composition tree.
}

std::unique_ptr<Pane> WindowManager::MakePane(text::Buffer& buffer, editor::Mode mode) {
    auto pane = std::make_unique<Pane>(
        buffer, killRing_, registers_, promptHistory_, bufferList_, registry_, janetKeymap_, globalKeymap_, std::move(mode),
        statusMessage_, theme_, projectSidebar_, lspManager_, taskRunner_, testRunner_, vcsRunner_, dapManager_, acpManager_,
        projectUndo_, janetEnv_,
        [this](editor::InteractiveRequest request) { HandleWindowRequest(request); },
        [this](text::Buffer& closedBuffer) { HandleBufferClosed(closedBuffer); });
    pane->SetEventLoop(eventLoop_);
    // VCS side panel: not threaded through the Pane constructor's own
    // parameter list like projectSidebar_ above -- wired post-construction
    // here instead, same as every other Set*-hook forward below.
    pane->Buffer().SetVcsPanel(vcsPanel_);
    pane->Buffer().SetThemeApplier(themeApplier_);
    pane->Buffer().SetOnTerminalToggle(onTerminalToggle_);
    pane->Buffer().SetOnAcpPanelToggle(onAcpPanelToggle_);
    pane->Buffer().SetOnAcpRewindRequest(onAcpRewindRequest_);
    pane->Buffer().SetOnDapConsoleToggle(onDapConsoleToggle_);
    pane->Buffer().SetOnBufferListToggle(onBufferListToggle_);
    pane->Buffer().SetOnPrefixHintChanged(onPrefixHintChanged_);
    pane->Buffer().SetOnCandidatesChanged(onCandidatesChanged_);
    pane->Buffer().SetOnCompletionChanged(onCompletionChanged_);
    pane->Buffer().SetOnHierarchyChanged(WireHierarchyCallback(pane.get()));
    pane->Buffer().SetOnPointerGraphChanged(WirePointerGraphCallback(pane.get()));
    // Split-resize follow-up: see WindowManager.h's own resizingSplit_
    // comment and BufferView::SetSplitResizeQuery's own doc comment.
    pane->Buffer().SetSplitResizeQuery([this] { return resizingSplit_; });
    return pane;
}

void WindowManager::SetProjectSidebar(ProjectSidebar* sidebar) {
    projectSidebar_ = sidebar;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetProjectSidebar(sidebar);
    }
}

void WindowManager::SetVcsPanel(VcsPanel* panel) {
    vcsPanel_ = panel;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetVcsPanel(panel);
    }
}

void WindowManager::SetLspManager(editor::lsp::LspManager* lspManager) {
    lspManager_ = lspManager;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetLspManager(lspManager);
        pane->ModeLineRef().SetLspManager(lspManager);
    }
    // edit-application-gaps follow-up: wires a server-pushed
    // workspace/applyEdit request through to whichever pane has focus, once
    // per LspManager (not per-pane -- the handler itself is one std::function
    // owned by LspManager, no pane-specific state to re-wire on split/close).
    if (lspManager_) {
        lspManager_->SetApplyEditHandler([this](const editor::lsp::LspManager::ResolvedRename& edit, const std::string& label) {
            return ApplyServerPushedWorkspaceEdit(edit, label);
        });
    }
}

void WindowManager::SetThemeApplier(std::function<void(const Theme&)> applier) {
    themeApplier_ = std::move(applier);
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetThemeApplier(themeApplier_);
    }
}

void WindowManager::SetOnTerminalToggle(std::function<void()> onToggle) {
    onTerminalToggle_ = std::move(onToggle);
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetOnTerminalToggle(onTerminalToggle_);
    }
}

void WindowManager::SetOnAcpPanelToggle(std::function<void()> onToggle) {
    onAcpPanelToggle_ = std::move(onToggle);
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetOnAcpPanelToggle(onAcpPanelToggle_);
    }
}

void WindowManager::SetOnAcpRewindRequest(std::function<void()> onRewind) {
    onAcpRewindRequest_ = std::move(onRewind);
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetOnAcpRewindRequest(onAcpRewindRequest_);
    }
}

void WindowManager::SetAcpPanelFocusChecker(std::function<bool()> checker) {
    acpPanelFocused_ = std::move(checker);
}

void WindowManager::SetOnDapConsoleToggle(std::function<void()> onToggle) {
    onDapConsoleToggle_ = std::move(onToggle);
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetOnDapConsoleToggle(onDapConsoleToggle_);
    }
}

void WindowManager::SetOnBufferListToggle(std::function<void()> onToggle) {
    onBufferListToggle_ = std::move(onToggle);
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetOnBufferListToggle(onBufferListToggle_);
    }
}

void WindowManager::SetOnPrefixHintChanged(std::function<void(std::optional<WhichKeyHint>)> onHintChanged) {
    onPrefixHintChanged_ = std::move(onHintChanged);
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetOnPrefixHintChanged(onPrefixHintChanged_);
    }
}

void WindowManager::SetOnCandidatesChanged(std::function<void(std::optional<ListPopupModel>)> onCandidatesChanged) {
    onCandidatesChanged_ = std::move(onCandidatesChanged);
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetOnCandidatesChanged(onCandidatesChanged_);
    }
}

void WindowManager::SetOnCompletionChanged(std::function<void(std::optional<ListPopupModel>)> onCompletionChanged) {
    onCompletionChanged_ = std::move(onCompletionChanged);
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetOnCompletionChanged(onCompletionChanged_);
    }
}

std::function<void(std::optional<TreeViewModel>)> WindowManager::WireHierarchyCallback(Pane* pane) {
    return [this, pane](std::optional<TreeViewModel> model) {
        hierarchyOwnerPane_ = model ? pane : nullptr;
        if (onHierarchyChanged_) {
            onHierarchyChanged_(std::move(model));
        }
    };
}

void WindowManager::SetOnHierarchyChanged(std::function<void(std::optional<TreeViewModel>)> onHierarchyChanged) {
    onHierarchyChanged_ = std::move(onHierarchyChanged);
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetOnHierarchyChanged(WireHierarchyCallback(pane));
    }
}

void WindowManager::HierarchyActivate(std::size_t index) {
    if (hierarchyOwnerPane_) {
        hierarchyOwnerPane_->Buffer().HierarchyActivate(index);
    }
}

void WindowManager::HierarchyToggleExpand(std::size_t index) {
    if (hierarchyOwnerPane_) {
        hierarchyOwnerPane_->Buffer().HierarchyToggleExpand(index);
    }
}

void WindowManager::HierarchyCollapse(std::size_t index) {
    if (hierarchyOwnerPane_) {
        hierarchyOwnerPane_->Buffer().HierarchyCollapse(index);
    }
}

void WindowManager::HierarchyCancel() {
    if (hierarchyOwnerPane_) {
        hierarchyOwnerPane_->Buffer().HierarchyCancel();
    }
}

void WindowManager::HierarchySelectionChanged(std::size_t index) {
    if (hierarchyOwnerPane_) {
        hierarchyOwnerPane_->Buffer().HierarchySelectionChanged(index);
    }
}

std::function<void(std::optional<TreeViewModel>)> WindowManager::WirePointerGraphCallback(Pane* pane) {
    return [this, pane](std::optional<TreeViewModel> model) {
        pointerGraphOwnerPane_ = model ? pane : nullptr;
        if (onPointerGraphChanged_) {
            onPointerGraphChanged_(std::move(model));
        }
    };
}

void WindowManager::SetOnPointerGraphChanged(std::function<void(std::optional<TreeViewModel>)> onPointerGraphChanged) {
    onPointerGraphChanged_ = std::move(onPointerGraphChanged);
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetOnPointerGraphChanged(WirePointerGraphCallback(pane));
    }
}

void WindowManager::PointerGraphActivate(std::size_t index) {
    if (pointerGraphOwnerPane_) {
        pointerGraphOwnerPane_->Buffer().PointerGraphActivate(index);
    }
}

void WindowManager::PointerGraphToggleExpand(std::size_t index) {
    if (pointerGraphOwnerPane_) {
        pointerGraphOwnerPane_->Buffer().PointerGraphToggleExpand(index);
    }
}

void WindowManager::PointerGraphCollapse(std::size_t index) {
    if (pointerGraphOwnerPane_) {
        pointerGraphOwnerPane_->Buffer().PointerGraphCollapse(index);
    }
}

void WindowManager::PointerGraphCancel() {
    if (pointerGraphOwnerPane_) {
        pointerGraphOwnerPane_->Buffer().PointerGraphCancel();
    }
}

void WindowManager::PointerGraphSelectionChanged(std::size_t index) {
    if (pointerGraphOwnerPane_) {
        pointerGraphOwnerPane_->Buffer().PointerGraphSelectionChanged(index);
    }
}

void WindowManager::SetTestRunner(editor::testrun::TestRunner* testRunner) {
    testRunner_ = testRunner;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetTestRunner(testRunner);
    }
}

void WindowManager::SetTaskRunner(editor::tasks::TaskRunner* taskRunner) {
    taskRunner_ = taskRunner;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetTaskRunner(taskRunner);
    }
}

void WindowManager::SetProjectUndo(editor::ProjectUndoManager* projectUndo) {
    projectUndo_ = projectUndo;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetProjectUndo(projectUndo);
    }
}

void WindowManager::SetVcsRunner(editor::vcs::VcsRunner* vcsRunner) {
    vcsRunner_ = vcsRunner;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetVcsRunner(vcsRunner);
    }
    if (projectSidebar_) {
        projectSidebar_->SetVcsRunner(vcsRunner);
    }
    if (vcsPanel_) {
        vcsPanel_->SetVcsRunner(vcsRunner);
    }
}

void WindowManager::SetDapManager(editor::dap::DapManager* dapManager) {
    dapManager_ = dapManager;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetDapManager(dapManager);
    }
    if (dapManager == nullptr) {
        return;
    }
    // The session's async outcomes land here (single-slot callbacks, and
    // WindowManager is the one owner that can resolve "the focused pane"
    // fresh at fire time -- a specific BufferView captured at wiring time
    // could be a pane that's since been split away or closed). Both run on
    // the main thread via DapClient's own Post-marshaling, after which
    // EventLoop::Run repaints unconditionally -- same as every other async
    // completion in this codebase.
    dapManager->SetOnStopped([this](const editor::dap::DapManager::StoppedInfo& info) {
        if (info.path) {
            // Status first, jump second: JumpToPathLine reports its own
            // failure via statusMessage_, and that error must survive, not
            // be overwritten by the happy-path text.
            statusMessage_ = "Stopped (" + info.reason + ") at " + info.path->filename().string() + ":" +
                             std::to_string(info.line);
            Pane* pane     = FocusedPane();
            if (pane == nullptr && !Leaves().empty()) {
                pane = Leaves().front();
            }
            if (pane != nullptr) {
                pane->Buffer().JumpToPathLine(*info.path, info.line);
            }
        }
        else {
            statusMessage_ = "Stopped (" + info.reason + ") -- no source location.";
        }
    });
    dapManager->SetOnSessionEnded([this](std::string reason) { statusMessage_ = std::move(reason); });
}

void WindowManager::SetAcpManager(editor::acp::AcpManager* acpManager) {
    acpManager_ = acpManager;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetAcpManager(acpManager);
    }
    if (acpManager == nullptr) {
        return;
    }
    // Same "resolve the focused pane fresh at fire time" reasoning as
    // SetDapManager's own SetOnStopped wiring just above -- a specific
    // BufferView captured when this was called could be a pane that's
    // since been split away or closed.
    acpManager->SetOnPermissionRequest([this](const editor::acp::AcpManager::PermissionPrompt& prompt) {
        // ACP round-1-live-validation follow-up: the AcpPanel, when focused,
        // resolves this itself (AcpPanel::OnEvent) -- see
        // SetAcpPanelFocusChecker's own doc comment. Routing to the pane's
        // echo area too would fight over the same single pending request.
        if (acpPanelFocused_ && acpPanelFocused_()) {
            return;
        }
        Pane* pane = FocusedPane();
        if (pane == nullptr && !Leaves().empty()) {
            pane = Leaves().front();
        }
        if (pane != nullptr) {
            pane->Buffer().ShowAcpPermissionPrompt(prompt);
        }
    });
    acpManager->SetOnSessionEnded([this](std::string reason) { statusMessage_ = std::move(reason); });
}

void WindowManager::SetLastKnownAcpAgent(std::optional<std::string> name) {
    lastAcpAgentSeed_ = std::move(name);
}

void WindowManager::SetJanetEnvironment(const janet::Environment* janetEnv) {
    janetEnv_ = janetEnv;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetJanetEnvironment(janetEnv);
    }
}

void WindowManager::SetEventLoop(EventLoop* eventLoop) {
    eventLoop_ = eventLoop;
    for (Pane* pane : Leaves()) {
        pane->SetEventLoop(eventLoop);
    }
}

Widget& WindowManager::RootComponent() {
    return rootComponent_;
}

std::size_t WindowManager::WindowCount() const {
    return Leaves().size();
}

void WindowManager::TakeFocus() {
    if (Pane* pane = FocusedPane()) {
        pane->Buffer().TakeFocus();
        return;
    }
    // No pane currently reports Focused() -- true the very first time this
    // is called (see this method's own header comment), since focus was
    // never successfully established at all yet. Fall back to the first
    // leaf.
    if (!Leaves().empty()) {
        Leaves().front()->Buffer().TakeFocus();
    }
}

ActiveBuffer& WindowManager::FocusedActiveBuffer() {
    if (Pane* pane = FocusedPane()) {
        return pane->ActiveBufferRef();
    }
    // Defensive fallback -- should be unreachable (there's always at least
    // one pane, and every tree mutation ends with an explicit TakeFocus()),
    // but returning a real ActiveBuffer rather than crashing keeps this
    // safe to call from anywhere.
    return Leaves().front()->ActiveBufferRef();
}

void WindowManager::RequestCloseBuffer(text::Buffer& buffer) {
    if (Pane* pane = FocusedPane()) {
        pane->Buffer().RequestCloseBuffer(buffer);
    }
}

void WindowManager::RequestVcsPanelAction(VcsPanelAction action) {
    if (Pane* pane = FocusedPane()) {
        pane->Buffer().RequestVcsAction(action);
    }
}

void WindowManager::RequestOpenBinaryFile(const std::filesystem::path& path) {
    if (Pane* pane = FocusedPane()) {
        pane->Buffer().RequestOpenBinaryFile(path);
    }
}

bool WindowManager::ApplyServerPushedWorkspaceEdit(const editor::lsp::LspManager::ResolvedRename& edit, const std::string& label) {
    if (Pane* pane = FocusedPane()) {
        return pane->Buffer().ApplyServerPushedWorkspaceEdit(edit, label);
    }
    return false; // no pane focused anywhere -- nowhere to route this
}

void WindowManager::TriggerSwitchProject() {
    if (Pane* pane = FocusedPane()) {
        pane->Buffer().TriggerSwitchProject();
    }
}

void WindowManager::ActivateCompletionAt(std::size_t index) {
    if (Pane* pane = FocusedPane()) {
        pane->Buffer().AcceptActiveCompletionAt(index);
    }
}

void WindowManager::RequestTrustProjectInit(
    const std::filesystem::path&                                                   initPath,
    std::function<void(const std::filesystem::path&, editor::ProjectInitDecision)> onDecision) {
    if (Pane* pane = FocusedPane()) {
        pane->Buffer().RequestTrustProjectInit(initPath, std::move(onDecision));
    }
}

void WindowManager::StartAutoSaveTimer(EventLoop& eventLoop) {
    autoSaveThread_ = std::jthread([this, &eventLoop](std::stop_token stopToken) {
        std::mutex                  mutex;
        std::condition_variable_any cv;
        while (!stopToken.stop_requested()) {
            std::unique_lock lock(mutex);
            if (cv.wait_for(lock, stopToken, kScratchAutoSaveInterval, [&stopToken] { return stopToken.stop_requested(); })) {
                return;
            }
            eventLoop.Post([this] {
                editor::AutoSaveScratchBuffers(bufferList_);
                // backup-and-recovery follow-up: crash-recovery snapshots
                // for regular file buffers ride the same tick (skips
                // unmodified/unchanged buffers via a generation memo, so
                // idle ticks cost nothing), plus the rate-limited backup
                // pruning (at most once per hour).
                editor::AutoSaveFileBuffers(bufferList_);
                editor::MaybePruneBackups();
                // diagnostics-log follow-up: same rate-limited-once-per-hour
                // posture as MaybePruneBackups above.
                editor::MaybePruneLogFiles();
                // external-modification-safety follow-up: piggybacked on
                // this same tick -- revert/merge any open buffer whose file
                // changed on disk (see SweepExternalChanges; also fired
                // instantly by the inotify watcher, this tick is the
                // safety-net path).
                SweepExternalChanges();
                // file-watcher follow-up: also the periodic watch-set
                // resync point -- a buffer opened since the last tick is
                // watched from here on (see ResyncFileWatcher's own doc
                // comment for why there's no buffer-open hook).
                ResyncFileWatcher();
                // session-persistence slices 1+2: piggybacked on this
                // existing tick rather than a second timer thread -- both
                // saves skip the disk write entirely when nothing changed.
                RecordSessionPlaces();
                editor::SaveFilePlaces();
                // editor-ergonomics follow-up: same "skip the write when
                // nothing changed" posture as SaveFilePlaces above.
                editor::SaveRecentFiles();
                editor::SaveBookmarks();
                // persistent-undo follow-up: same "skip the write when
                // nothing changed" posture as SaveFilePlaces above (its own
                // memo is per-buffer, via ContentGeneration()).
                editor::SaveUndoHistoryForOpenBuffers(bufferList_);
                SaveProjectSessionNow();
                // subprocess-hang-protection follow-up: same tick, same
                // "unattended sweep" posture -- resolves any LSP/DAP/ACP
                // request that's been waiting past ProcessTimeouts.h's
                // ProtocolRequestTimeoutMs() with a synthetic timeout failure
                // instead of leaving it permanently pending. A no-op on
                // every ordinary tick (no request outstanding that long).
                if (lspManager_) {
                    lspManager_->ExpireStaleRequests();
                    // LSP-deliberate-cuts follow-up: widens SyncBuffer past
                    // the pane-active buffer BufferView::Paint() already
                    // syncs every frame -- every other open, loaded,
                    // path-backed buffer gets synced here instead, so a
                    // background tab's diagnostics/completions stay current
                    // without the user ever switching to it. Toggle-gated
                    // (ned/set-lsp-sync-background-buffers, default on); see
                    // LspBackgroundSync.h for why this can't just be another
                    // per-frame BufferView call.
                    editor::lsp::SyncBackgroundBuffers(bufferList_, *lspManager_);
                }
                if (dapManager_) {
                    dapManager_->ExpireStaleRequests();
                }
                if (acpManager_) {
                    acpManager_->ExpireStaleRequests();
                }
            });
        }
    });
}

void WindowManager::StartFileWatcher(EventLoop& eventLoop) {
    fileWatcher_ = std::make_unique<editor::FileWatcher>([this, &eventLoop] {
        eventLoop.Post([this] {
            SweepExternalChanges();
            ResyncFileWatcher();
        });
    });
    ResyncFileWatcher();
}

void WindowManager::ResyncFileWatcher() {
    if (!fileWatcher_) {
        return;
    }
    std::vector<std::filesystem::path> files;
    if (editor::FileWatchEnabled()) {
        for (const auto& buffer : bufferList_.Buffers()) {
            if (buffer->Path().has_value()) {
                files.push_back(*buffer->Path());
            }
        }
    }
    fileWatcher_->SetWatchedFiles(files);
}

void WindowManager::SweepExternalChanges() {
    // Reload any open, *unmodified* buffer whose file changed on disk
    // (default on; see AutoRevert.h). Surfaced via the shared status line
    // so a buffer changing underneath the user is never silent.
    if (const std::vector<std::string> reverted = editor::AutoRevertBuffers(bufferList_); !reverted.empty()) {
        std::string names;
        for (const std::string& name : reverted) {
            names += names.empty() ? name : ", " + name;
        }
        statusMessage_ = "Reverted (changed on disk): " + names;
    }
    // external-modification-round-2 follow-up: the conflicting half
    // AutoRevertBuffers deliberately skips (a buffer with local edits AND a
    // file that also changed) -- a three-way merge (default on; see
    // AutoMerge.h) instead of a discard. Clean and conflicted merges get
    // distinct status text so a conflict (unresolved "<<<<<<<" markers now
    // sitting in the buffer) is never mistaken for a silent, fully-
    // automatic one.
    if (const std::vector<editor::AutoMergeResult> merged = editor::AutoMergeBuffers(bufferList_); !merged.empty()) {
        std::string clean;
        std::string conflicted;
        for (const editor::AutoMergeResult& result : merged) {
            if (result.conflictCount == 0) {
                clean += clean.empty() ? result.name : ", " + result.name;
            }
            else {
                conflicted +=
                    (conflicted.empty() ? "" : ", ") + result.name + " (" + std::to_string(result.conflictCount) + ")";
            }
        }
        std::string message;
        if (!clean.empty()) {
            message = "Merged external changes: " + clean;
        }
        if (!conflicted.empty()) {
            message += (message.empty() ? "" : "; ") + std::string("conflict(s) in ") + conflicted +
                       " -- resolve <<<<<<< markers";
        }
        statusMessage_ = message;
    }
    // vcs-diff-gutter-staleness follow-up: same "unattended sweep, cheap
    // when nothing changed" posture as the two calls above -- an external
    // change stales the diff gutter too. See RefreshVcsDiffGutters' own doc
    // comment in the header.
    RefreshVcsDiffGutters();
}

void WindowManager::RefreshVcsDiffGutters() {
    for (Pane* pane : Leaves()) {
        pane->Buffer().RefreshVcsDiff();
    }
}

void WindowManager::RecordSessionPlaces() {
    if (!editor::SavePlaceEnabled()) {
        return;
    }

    const auto tabWidth = static_cast<std::size_t>(editor::TabWidth());
    for (const auto& buffer : bufferList_.Buffers()) {
        editor::RecordFilePlace(*buffer, std::nullopt, tabWidth);
    }
    // Second pass wins for visible buffers: same place, plus the viewport.
    for (Pane* pane : Leaves()) {
        editor::RecordFilePlace(pane->ActiveBufferRef().Get(), pane->Buffer().TopLine(), tabWidth);
    }
}

void WindowManager::SaveProjectSessionNow() {
    // Cheap early-outs; SaveActiveProjectSession itself re-checks both under
    // its own lock, so these are an optimization, not the guard.
    if (!editor::SessionRestoreEnabled() || !editor::ActiveProjectSessionRoot()) {
        return;
    }

    // Scratch pads are global, session-independent state (ScratchPad.h) --
    // a project session re-opening them would be surprising. Same
    // directly-inside comparison AutoSaveScratchBuffers makes; the whole
    // exclusion is best-effort (ScratchDirectory can throw with no HOME).
    std::optional<std::filesystem::path> scratchDirectory;
    try {
        scratchDirectory = std::filesystem::weakly_canonical(editor::ScratchDirectory());
    }
    catch (const std::exception&) {
    }

    editor::ProjectSessionData data;
    const text::Buffer*        preview = bufferList_.PreviewBuffer();
    for (const auto& buffer : bufferList_.Buffers()) {
        if (!buffer->Path() || buffer.get() == preview) {
            continue;
        }
        std::error_code ec;
        if (scratchDirectory &&
            std::filesystem::weakly_canonical(buffer->Path()->parent_path(), ec) == *scratchDirectory) {
            continue;
        }
        data.openFiles.push_back(std::filesystem::absolute(*buffer->Path()));
    }

    if (const auto& activePath = FocusedActiveBuffer().Get().Path()) {
        data.activeFile = std::filesystem::absolute(*activePath);
    }

    if (projectSidebar_ != nullptr) {
        // Chrome-redesign follow-up: the stored visibility bool now maps
        // onto the collapse state (active stays permanently true), and the
        // stored width is the real expanded width, not the 1-column strip
        // Width() reports while collapsed.
        data.sidebarVisible = !projectSidebar_->Collapsed();
        data.sidebarWidth   = projectSidebar_->ExpandedWidth();
    }

    if (dapManager_ != nullptr) {
        // session-persistence round 2: DapManager::PersistedBreakpoint ->
        // editor::BreakpointState per entry -- same shape, different
        // namespace (ProjectSession.h stays Dap-header-free, see
        // BreakpointState's own doc comment).
        for (const auto& [key, entries] : dapManager_->AllBreakpoints()) {
            std::vector<editor::BreakpointState>& out = data.breakpoints[key];
            for (const auto& bp : entries) {
                out.push_back(editor::BreakpointState{
                    .line         = bp.line,
                    .condition    = bp.condition,
                    .logMessage   = bp.logMessage,
                    .hitCondition = bp.hitCondition,
                });
            }
        }
        data.watches = dapManager_->Watches();
    }

    // ACP auto-reconnect follow-up: AgentName() is sticky for the rest of
    // this process once a session has ever been started (AcpManager.cpp's
    // own StartSession is the only assignment site, never cleared by
    // StopSession) -- prefer it live whenever set, falling back to
    // lastAcpAgentSeed_ (what SetLastKnownAcpAgent seeded from the
    // previously *saved* value) so a run that never touches ACP at all
    // doesn't clobber a project's remembered agent back to nullopt, since
    // `data` here is built fresh from scratch every save rather than loaded
    // first.
    if (acpManager_ != nullptr && !acpManager_->AgentName().empty()) {
        data.lastAcpAgent = acpManager_->AgentName();
    }
    else {
        data.lastAcpAgent = lastAcpAgentSeed_;
    }

    CaptureWindowLayout(data);

    editor::SaveActiveProjectSession(data);
}

namespace {

    // Post-order: appends node's children before node itself, so every
    // index a parent records for its children is already < the parent's own
    // (freshly-appended) index -- see WindowLayoutNode's own doc comment.
    // Returns nullopt (aborting the whole capture) the moment any leaf's
    // buffer has no path.
    std::optional<std::size_t> CaptureLayoutNode(const WindowNode& node, std::vector<editor::WindowLayoutNode>& out) {
        if (node.kind == WindowNode::Kind::Leaf) {
            const std::optional<std::filesystem::path> path = node.pane->ActiveBufferRef().Get().Path();
            if (!path) {
                return std::nullopt;
            }
            editor::WindowLayoutNode entry;
            entry.kind = editor::WindowLayoutNode::Kind::Leaf;
            entry.file = std::filesystem::absolute(*path);
            out.push_back(std::move(entry));
            return out.size() - 1;
        }

        const std::optional<std::size_t> first = CaptureLayoutNode(*node.first, out);
        if (!first) {
            return std::nullopt;
        }
        const std::optional<std::size_t> second = CaptureLayoutNode(*node.second, out);
        if (!second) {
            return std::nullopt;
        }
        editor::WindowLayoutNode entry;
        entry.kind   = node.kind == WindowNode::Kind::SplitBelow ? editor::WindowLayoutNode::Kind::SplitBelow
                                                                 : editor::WindowLayoutNode::Kind::SplitRight;
        entry.first  = first;
        entry.second = second;
        entry.ratio  = node.ratio; // split-resize follow-up: survive a session restore, not just this run
        out.push_back(std::move(entry));
        return out.size() - 1;
    }

    // Walks down from node building path (0 = first, 1 = second) until it
    // reaches the leaf holding focused -- true the moment path is complete,
    // leaving path untouched-beyond-its-prefix on a dead end so the caller's
    // own push/pop backtracking stays simple.
    bool FindFocusedPath(const WindowNode& node, const Pane* focused, std::vector<int>& path) {
        if (node.kind == WindowNode::Kind::Leaf) {
            return node.pane.get() == focused;
        }
        path.push_back(0);
        if (FindFocusedPath(*node.first, focused, path)) {
            return true;
        }
        path.back() = 1;
        if (FindFocusedPath(*node.second, focused, path)) {
            return true;
        }
        path.pop_back();
        return false;
    }

} // namespace

void WindowManager::CaptureWindowLayout(editor::ProjectSessionData& data) {
    std::vector<editor::WindowLayoutNode> layout;
    if (!CaptureLayoutNode(*root_, layout)) {
        return; // some leaf's buffer has no path -- see this method's own header comment
    }
    data.windowLayout = std::move(layout);

    data.focusedPanePath.clear();
    if (const Pane* focused = FocusedPane()) {
        std::vector<int> path;
        if (FindFocusedPath(*root_, focused, path)) {
            data.focusedPanePath = std::move(path);
        }
    }
}

std::unique_ptr<WindowNode> WindowManager::BuildNodeFromLayout(const std::vector<editor::WindowLayoutNode>& nodes, std::size_t index) {
    if (index >= nodes.size()) {
        return nullptr;
    }
    const editor::WindowLayoutNode& entry = nodes[index];

    if (entry.kind == editor::WindowLayoutNode::Kind::Leaf) {
        if (!entry.file) {
            return nullptr;
        }
        text::Buffer* buffer = bufferList_.FindByPath(*entry.file);
        if (buffer == nullptr) {
            return nullptr;
        }
        auto node  = std::make_unique<WindowNode>();
        node->kind = WindowNode::Kind::Leaf;
        node->pane = MakePane(*buffer, editor::CachedModeForBuffer(*buffer));
        return node;
    }

    // Guards a corrupted/malformed file's forward-or-self-referencing index
    // against recursing forever -- ProjectSessionFromJson already enforces
    // this on load, this is defense in depth against any other caller.
    if (!entry.first || !entry.second || *entry.first >= index || *entry.second >= index) {
        return nullptr;
    }
    std::unique_ptr<WindowNode> first  = BuildNodeFromLayout(nodes, *entry.first);
    std::unique_ptr<WindowNode> second = BuildNodeFromLayout(nodes, *entry.second);
    if (!first || !second) {
        return nullptr;
    }
    auto node    = std::make_unique<WindowNode>();
    node->kind   = entry.kind == editor::WindowLayoutNode::Kind::SplitBelow ? WindowNode::Kind::SplitBelow : WindowNode::Kind::SplitRight;
    node->first  = std::move(first);
    node->second = std::move(second);
    node->ratio  = std::clamp(entry.ratio, 0.1f, 0.9f); // clamp defends against a hand-edited/corrupted session file
    return node;
}

void WindowManager::RestoreWindowLayout(const editor::ProjectSessionData& data) {
    if (data.windowLayout.empty()) {
        return;
    }
    std::unique_ptr<WindowNode> newRoot = BuildNodeFromLayout(data.windowLayout, data.windowLayout.size() - 1);
    if (!newRoot) {
        return; // some referenced file wasn't open -- leave the existing default single-pane root_ alone
    }
    root_ = std::move(newRoot);
    RebuildComponentTree();

    WindowNode* target = root_.get();
    for (const int choice : data.focusedPanePath) {
        if (target->kind == WindowNode::Kind::Leaf) {
            break; // a path longer than the actual tree depth -- stop where the tree does
        }
        target = choice == 0 ? target->first.get() : target->second.get();
    }
    while (target->kind != WindowNode::Kind::Leaf) {
        target = target->first.get(); // an empty/too-short path -- fall back to the first leaf
    }
    target->pane->Buffer().TakeFocus();
}

void WindowManager::EnableAsyncFileLoading(EventLoop& eventLoop) {
    bufferList_.SetAsyncFileOpener([this, &eventLoop](text::Buffer& placeholder, const std::filesystem::path& path) {
        PurgeFinishedAsyncLoaders();
        asyncFileLoaders_.push_back(std::make_unique<AsyncFileLoader>(placeholder, bufferList_, path, eventLoop));
    });
}

void WindowManager::PurgeFinishedAsyncLoaders() {
    std::erase_if(asyncFileLoaders_, [](const std::unique_ptr<AsyncFileLoader>& loader) { return loader->Done(); });
}

void WindowManager::EnableAsyncHugeFileLoading(EventLoop& eventLoop) {
    bufferList_.SetAsyncHugeFileOpener(
        [this, &eventLoop](text::Buffer& placeholder, const std::filesystem::path& path, bool allowBinary) {
            PurgeFinishedHugeFileLoaders();
            hugeFileLoaders_.push_back(std::make_unique<HugeFileLoader>(placeholder, bufferList_, path, allowBinary, eventLoop));
        });
}

void WindowManager::PurgeFinishedHugeFileLoaders() {
    std::erase_if(hugeFileLoaders_, [](const std::unique_ptr<HugeFileLoader>& loader) { return loader->Done(); });
}

void WindowManager::HandleWindowRequest(editor::InteractiveRequest request) {
    switch (request) {
        case editor::InteractiveRequest::SplitBelow:
            SplitBelow();
            return;
        case editor::InteractiveRequest::SplitRight:
            SplitRight();
            return;
        case editor::InteractiveRequest::DeleteWindow:
            DeleteWindow();
            return;
        case editor::InteractiveRequest::DeleteOtherWindows:
            DeleteOtherWindows();
            return;
        case editor::InteractiveRequest::OtherWindow:
            OtherWindow();
            return;
        case editor::InteractiveRequest::EnlargeWindow:
            ResizeFocusedWindow(WindowNode::Kind::SplitBelow, /*grow=*/true);
            return;
        case editor::InteractiveRequest::ShrinkWindow:
            ResizeFocusedWindow(WindowNode::Kind::SplitBelow, /*grow=*/false);
            return;
        case editor::InteractiveRequest::EnlargeWindowHorizontally:
            ResizeFocusedWindow(WindowNode::Kind::SplitRight, /*grow=*/true);
            return;
        case editor::InteractiveRequest::ShrinkWindowHorizontally:
            ResizeFocusedWindow(WindowNode::Kind::SplitRight, /*grow=*/false);
            return;
        default:
            return; // BufferView only ever forwards these nine
    }
}

void WindowManager::ReassignPanesShowing(text::Buffer& closingBuffer, Pane* skip) {
    // Multibuffers follow-up: the one choke point both HandleBufferClosed
    // and NotifyBufferClosing funnel through for any real close, so this is
    // where a multibuffer's MultibufferIndex (if any -- a safe no-op
    // otherwise) gets cleared rather than dangling on a freed Buffer*.
    editor::multibuffer::ClearMultibufferIndexFor(closingBuffer);
    // per-buffer-mode-cache/per-buffer-highlight-cache follow-up: same
    // "close funnel clears every per-buffer cache keyed by this Buffer*"
    // precedent as the multibuffer index just above -- see
    // CachedModeForBuffer's/Pane::ClearBufferCaches's own doc comments.
    // Every pane is cleared, not just ones currently showing closingBuffer
    // (skipped via `skip` below) -- a pane can hold a stale cache entry for
    // a buffer it merely visited in the past, not just its current one.
    editor::ClearModeCacheFor(closingBuffer);
    for (Pane* pane : Leaves()) {
        pane->ClearBufferCaches(closingBuffer);
    }

    // Computed once, shared across every affected pane -- not recomputed
    // (and not re-created, in the CreateBuffer("scratch") fallback case)
    // per pane, so N panes all showing the one buffer being closed end up
    // sharing a single fresh scratch buffer rather than each conjuring
    // their own.
    //
    // MRU, matching BufferView::CloseBufferNow's own replacement pick --
    // otherwise a pane retargeted here (not the one the user actually
    // closed the buffer in) could land on an arbitrary, possibly long-stale
    // buffer instead of the one most recently used. Falls back to list
    // order for buffers never activated, same as CloseBufferNow.
    text::Buffer* replacement = bufferList_.MostRecentlyUsedBuffer(&closingBuffer);
    if (replacement == nullptr) {
        for (const auto& candidate : bufferList_.Buffers()) {
            if (candidate.get() != &closingBuffer) {
                replacement = candidate.get();
                break;
            }
        }
    }

    for (Pane* pane : Leaves()) {
        if (pane == skip) {
            continue;
        }
        if (&pane->ActiveBufferRef().Get() != &closingBuffer) {
            continue;
        }
        if (replacement == nullptr) {
            replacement = &bufferList_.CreateBuffer("scratch");
        }
        pane->ActiveBufferRef().Set(*replacement);
    }
}

void WindowManager::HandleBufferClosed(text::Buffer& closedBuffer) {
    // LSP client follow-up: sends textDocument/didClose (if this buffer was
    // ever synced) before any pane reassignment below touches it.
    if (lspManager_) {
        lspManager_->NotifyBufferClosed(closedBuffer);
    }
    // The pane whose own CloseBufferNow triggered this already handles its
    // own ActiveBuffer reassignment independently -- skip it here so a
    // single-buffer-in-the-whole-app close doesn't conjure two separate
    // fresh scratch buffers (one from each of us).
    ReassignPanesShowing(closedBuffer, FocusedPane());
}

void WindowManager::NotifyBufferClosing(text::Buffer& closingBuffer) {
    // Unlike HandleBufferClosed (wired to BufferView::SetOnBufferClosed,
    // where CloseBufferNow always separately handles its own pane), nothing
    // else reassigns anyone here -- ProjectSidebar::OpenFileEntry closes the
    // outgoing preview buffer directly (bufferList_.Close(oldPreview->
    // Name())), with no equivalent self-reassignment step for the pane it's
    // actually retargeting either (that pane gets pointed at the *new* file
    // moments later, a completely separate assignment). So every pane
    // showing the closing buffer needs handling here, none skipped -- a
    // real, confirmed bug fixed by adding this: any *other* pane (not the
    // one the sidebar click targets) that happened to also be showing the
    // outgoing preview was left with a dangling ActiveBuffer once
    // bufferList_.Close() actually freed it, crashing (heap corruption
    // manifesting inside ModeLine::Paint's own string building, confirmed
    // via two real coredumps, not guessed) the next time that pane's own
    // ModeLine/BufferView painted.
    // LSP client follow-up: same reasoning as HandleBufferClosed above --
    // this is a real close too (ProjectSidebar's own bufferList_.Close()
    // call), it just isn't pane-driven.
    if (lspManager_) {
        lspManager_->NotifyBufferClosed(closingBuffer);
    }
    ReassignPanesShowing(closingBuffer, nullptr);
}

void WindowManager::DoSplit(WindowNode::Kind kind) {
    Pane* focused = FocusedPane();
    if (focused == nullptr) {
        return;
    }

    // The new pane starts on the same buffer and the same Mode as the one
    // being split -- Emacs' own split-window semantics exactly (the new
    // window is a second view onto what you were already looking at, not a
    // blank slate). Mode is copied, not shared by reference -- see
    // WindowManager.h's own header comment on the Mode-per-pane scope
    // decision this project made for window-splitting.
    std::unique_ptr<Pane> newPane = MakePane(focused->ActiveBufferRef().Get(), focused->ModeRef());

    // Splitting keeps focus on the *original* pane -- Emacs' own
    // split-window-below/-right semantics: the new window does not steal
    // focus.
    Pane* toRefocus = focused;

    SplitLeafInTree(root_, focused, kind, newPane);
    RebuildComponentTree();
    toRefocus->Buffer().TakeFocus();
}

void WindowManager::SplitBelow() {
    DoSplit(WindowNode::Kind::SplitBelow);
}

void WindowManager::SplitRight() {
    DoSplit(WindowNode::Kind::SplitRight);
}

void WindowManager::DeleteWindow() {
    Pane* focused = FocusedPane();
    if (focused == nullptr) {
        return;
    }
    if (root_->kind == WindowNode::Kind::Leaf) {
        // Mirrors RequestCloseBuffer's own "reports via statusMessage_
        // instead of silently doing nothing" precedent for a comparable
        // can't-do-that-right-now case.
        statusMessage_ = "Cannot delete the only window.";
        return;
    }

    Pane* toRefocus = FindSiblingFirstLeaf(root_.get(), focused);

    DeleteLeafInTree(root_, focused);
    RebuildComponentTree();
    if (toRefocus != nullptr) {
        toRefocus->Buffer().TakeFocus();
    }
}

void WindowManager::DeleteOtherWindows() {
    Pane* focused = FocusedPane();
    if (focused == nullptr) {
        return;
    }
    if (root_->kind == WindowNode::Kind::Leaf) {
        return; // already the only window -- nothing to do
    }

    std::unique_ptr<Pane> survivor = ExtractPane(root_, focused);

    auto newRoot  = std::make_unique<WindowNode>();
    newRoot->kind = WindowNode::Kind::Leaf;
    newRoot->pane = std::move(survivor);
    root_         = std::move(newRoot);

    RebuildComponentTree();
    focused->Buffer().TakeFocus();
}

void WindowManager::OtherWindow() {
    std::vector<Pane*> leaves = Leaves();
    if (leaves.size() < 2) {
        return;
    }
    for (std::size_t i = 0; i < leaves.size(); ++i) {
        if (leaves[i]->Buffer().Focused()) {
            leaves[(i + 1) % leaves.size()]->Buffer().TakeFocus();
            return;
        }
    }
}

void WindowManager::ResizeFocusedWindow(WindowNode::Kind axis, bool grow) {
    Pane* focused = FocusedPane();
    if (focused == nullptr) {
        return;
    }
    const std::optional<SplitAncestor> ancestor = FindNearestSplitAncestor(root_.get(), focused, axis);
    if (!ancestor) {
        return; // no split along this axis above the focused pane -- e.g. enlarge-window in a single window
    }

    // Growing target's own side means growing `first`'s ratio when target
    // is in `first`, shrinking it (so `second` grows instead) when target
    // is in `second` -- and shrinking is just the same step in reverse.
    constexpr float kResizeStep = 0.02f;
    const float     step        = (grow == ancestor->targetInFirst) ? kResizeStep : -kResizeStep;

    // Fixed fractional bounds here (not the mouse-drag path's adaptive
    // pixel floor) -- a keyboard command has no live Box_() guaranteed
    // (e.g. before the very first Paint()), so this can't scale off actual
    // terminal size the way SplitDivider::UpdateResize does; 10%/90% is a
    // sane floor regardless of terminal size.
    ancestor->node->ratio = std::clamp(ancestor->node->ratio + step, 0.1f, 0.9f);
}

void WindowManager::RebuildComponentTree() {
    // SizeSpec::Flex() on the child itself is what flexes it to fill
    // rootComponent_, the same way BuildComponent's own Split cases already
    // give each of their two children a Flex weight.
    rootComponent_.SetChildren({
        {&BuildComponent(root_.get()), SizeSpec::Flex()},
    });
}

Widget& WindowManager::BuildComponent(WindowNode* node) const {
    if (node->kind == WindowNode::Kind::Leaf) {
        return node->pane->Component();
    }

    Widget& first  = BuildComponent(node->first.get());
    Widget& second = BuildComponent(node->second.get());

    // Split-resize follow-up: `first` is sized directly from node->ratio,
    // re-read fresh every Paint() (see WindowNode::ratio's own doc comment)
    // -- `second` takes SizeSpec::Flex(), the remainder after `first`'s
    // DynamicFixed size and the divider's own Fixed(1) are subtracted, the
    // same "whatever's left" mechanism every other Flex child in this
    // codebase already relies on. Captures the raw WindowNode* node itself
    // (heap-owned by its parent, never moved once built), reading
    // node->container->Box_() -- valid by the time this runs: it's called
    // from *node->container's own* LayoutChildren, and a Container's Box_()
    // is always set by its parent before Paint()/LayoutChildren ever run on
    // it (see Layout.h's own header comment).
    auto firstSize = [node]() -> int {
        if (!node->container) {
            return 0;
        }
        const Box& box       = node->container->Box_();
        const bool vertical  = node->kind == WindowNode::Kind::SplitRight;
        const int  totalLen  = vertical ? (box.x_max - box.x_min + 1) : (box.y_max - box.y_min + 1);
        const int  available = std::max(0, totalLen - 1); // minus the divider's own 1-cell thickness
        return static_cast<int>(std::lround(node->ratio * available));
    };

    if (node->kind == WindowNode::Kind::SplitRight) {
        if (!node->divider) {
            node->divider = std::make_unique<SplitDivider>(theme_, /*vertical=*/true, *node,
                                                           [this](bool active) { resizingSplit_ = active; });
        }
        if (!node->container) {
            node->container = std::make_unique<Container>(Axis::Horizontal, std::vector<Container::Child>{});
        }
        node->container->SetChildren({
            {&first, SizeSpec::DynamicFixed(firstSize)},
            {node->divider.get(), SizeSpec::Fixed(1)},
            {&second, SizeSpec::Flex()},
        });
        return *node->container;
    }

    // SplitBelow -- previously relied on the top pane's own ModeLine row as
    // the visual boundary, no separate divider; split-resize follow-up gives
    // it a real, WindowNode-owned row instead (see SplitDivider's own header
    // comment for why that row is what makes drag-resize possible here).
    if (!node->divider) {
        node->divider = std::make_unique<SplitDivider>(theme_, /*vertical=*/false, *node,
                                                       [this](bool active) { resizingSplit_ = active; });
    }
    if (!node->container) {
        node->container = std::make_unique<Container>(Axis::Vertical, std::vector<Container::Child>{});
    }
    node->container->SetChildren({
        {&first, SizeSpec::DynamicFixed(firstSize)},
        {node->divider.get(), SizeSpec::Fixed(1)},
        {&second, SizeSpec::Flex()},
    });
    return *node->container;
}

bool WindowManager::FocusedPaneMinimapActive() {
    Pane* pane = FocusedPane();
    return pane != nullptr && pane->MinimapActive();
}

bool WindowManager::FocusedPaneScrollColumnActive() {
    Pane* pane = FocusedPane();
    return pane != nullptr && pane->ScrollColumnActive();
}

bool WindowManager::HasFocusedPane() {
    return FocusedPane() != nullptr;
}

Pane* WindowManager::FocusedPane() {
    for (Pane* pane : Leaves()) {
        if (pane->Buffer().Focused()) {
            return pane;
        }
    }
    return nullptr;
}

std::vector<Pane*> WindowManager::Leaves() const {
    std::vector<Pane*> result;
    CollectLeaves(root_.get(), result);
    return result;
}

void WindowManager::ReleaseMinimapPixelPlanes() {
    // main.cpp's local-variable declaration order (windowManager constructed
    // before EventLoop -- a pre-existing, deliberate ordering elsewhere in
    // that file's own carefully-sequenced setup) means locals are destroyed
    // in the *opposite* order at shutdown: ~EventLoop (which calls
    // notcurses_stop, freeing every plane and pile it owns) runs before this
    // WindowManager and its Panes/Minimaps do. A Minimap pixel-blitter plane
    // torn down that late would call ncplane_destroy on memory Notcurses
    // already freed -- a real, confirmed SIGABRT on exit, not a
    // hypothetical one. main.cpp calls this explicitly right after
    // eventLoop.Run() returns, while the Notcurses context is still
    // guaranteed alive, so every live plane is gone well before ~EventLoop
    // ever runs; ~Minimap()'s own ReleasePlane() call becomes a no-op
    // by the time it fires.
    for (Pane* pane : Leaves()) {
        pane->ReleaseMinimapPixelPlane();
    }
}

} // namespace ned::ui
