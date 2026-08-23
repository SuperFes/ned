#include "WindowManager.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

#include "Editor/AutoMerge.h"
#include "Editor/AutoRevert.h"
#include "Editor/Backup.h"
#include "Editor/Dap/DapManager.h"
#include "Editor/MinimapSettings.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Multibuffer.h"
#include "Editor/ProjectSession.h"
#include "Editor/ScratchPad.h"
#include "Editor/Session.h"
#include "Editor/TabWidth.h"

namespace ned::ui {

namespace {

    // See WindowManager::StartAutoSaveTimer's own header comment for why
    // this moved here, verbatim, from BufferView.
    constexpr std::chrono::milliseconds kScratchAutoSaveInterval{5000};

    // The one-column vertical divider between a SplitRight's two children --
    // chrome-redesign follow-up: now drawn with the theme's border brush so
    // split dividers, the sidebar frame, and the tab underline all read as
    // one line family (was untouched Color::Default, matching FTXUI's own
    // colorless separator()).
    class VerticalDivider : public Widget {
      public:
        explicit VerticalDivider(const Theme& theme) : theme_(theme) {
        }

        void Paint(Canvas c) override {
            for (int y = 0; y < c.size().height; ++y) {
                Cell& cell     = c[{.x = 0, .y = y}];
                cell.character = "│"; // BOX DRAWINGS LIGHT VERTICAL
                theme_.border.ApplyTo(cell);
            }
        }

      private:
        const Theme& theme_;
    };

} // namespace

Pane::Pane(text::Buffer& buffer, text::KillRing& killRing, editor::RegisterTable& registers,
           editor::PromptHistory& promptHistory, text::BufferList& bufferList, const editor::CommandRegistry& registry,
           const editor::Keymap& janetKeymap, const editor::Keymap& globalKeymap, editor::Mode mode,
           std::string& statusMessage, const Theme& theme, ProjectSidebar* projectSidebar,
           editor::lsp::LspManager* lspManager, editor::tasks::TaskRunner* taskRunner,
           editor::vcs::VcsRunner* vcsRunner, editor::dap::DapManager* dapManager, editor::acp::AcpManager* acpManager,
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
    activeBuffer_.SetOnChange([&bufferList](text::Buffer& current) { bufferList.TouchBuffer(current); });
    bufferList.TouchBuffer(buffer);
    // Chrome-redesign follow-up: this pane's mode line accent-tints its
    // gradient while this pane's own BufferView holds the keyboard focus --
    // the raw pointer outlives modeLine_ (both are members of this Pane).
    modeLine_->SetFocusProvider([view = bufferView_.get()] { return view->Focused(); });
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
    bufferView_->SetVcsRunner(vcsRunner);
    bufferView_->SetDapManager(dapManager);
    bufferView_->SetAcpManager(acpManager);
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
        statusMessage_, theme_, projectSidebar_, lspManager_, taskRunner_, vcsRunner_, dapManager_, acpManager_,
        [this](editor::InteractiveRequest request) { HandleWindowRequest(request); },
        [this](text::Buffer& closedBuffer) { HandleBufferClosed(closedBuffer); });
    pane->SetEventLoop(eventLoop_);
    pane->Buffer().SetThemeApplier(themeApplier_);
    pane->Buffer().SetOnTerminalToggle(onTerminalToggle_);
    pane->Buffer().SetOnAcpPanelToggle(onAcpPanelToggle_);
    return pane;
}

void WindowManager::SetProjectSidebar(ProjectSidebar* sidebar) {
    projectSidebar_ = sidebar;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetProjectSidebar(sidebar);
    }
}

void WindowManager::SetLspManager(editor::lsp::LspManager* lspManager) {
    lspManager_ = lspManager;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetLspManager(lspManager);
        pane->ModeLineRef().SetLspManager(lspManager);
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

void WindowManager::SetTaskRunner(editor::tasks::TaskRunner* taskRunner) {
    taskRunner_ = taskRunner;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetTaskRunner(taskRunner);
    }
}

void WindowManager::SetVcsRunner(editor::vcs::VcsRunner* vcsRunner) {
    vcsRunner_ = vcsRunner;
    for (Pane* pane : Leaves()) {
        pane->Buffer().SetVcsRunner(vcsRunner);
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

void WindowManager::RequestOpenBinaryFile(const std::filesystem::path& path) {
    if (Pane* pane = FocusedPane()) {
        pane->Buffer().RequestOpenBinaryFile(path);
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
                // external-modification-safety follow-up: piggybacked on
                // this same tick -- reload any open, *unmodified* buffer
                // whose file changed on disk (default on; see
                // AutoRevert.h). Surfaced via the shared status line so a
                // buffer changing underneath the user is never silent.
                if (const std::vector<std::string> reverted = editor::AutoRevertBuffers(bufferList_); !reverted.empty()) {
                    std::string names;
                    for (const std::string& name : reverted) {
                        names += names.empty() ? name : ", " + name;
                    }
                    statusMessage_ = "Reverted (changed on disk): " + names;
                }
                // external-modification-round-2 follow-up: the conflicting
                // half AutoRevertBuffers deliberately skips (a buffer with
                // local edits AND a file that also changed) -- a three-way
                // merge (default on; see AutoMerge.h) instead of a discard.
                // Clean and conflicted merges get distinct status text so a
                // conflict (unresolved "<<<<<<<" markers now sitting in the
                // buffer) is never mistaken for a silent, fully-automatic
                // one.
                if (const std::vector<editor::AutoMergeResult> merged = editor::AutoMergeBuffers(bufferList_);
                    !merged.empty()) {
                    std::string clean;
                    std::string conflicted;
                    for (const editor::AutoMergeResult& result : merged) {
                        if (result.conflictCount == 0) {
                            clean += clean.empty() ? result.name : ", " + result.name;
                        }
                        else {
                            conflicted += (conflicted.empty() ? "" : ", ") + result.name + " (" +
                                          std::to_string(result.conflictCount) + ")";
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
                // session-persistence slices 1+2: piggybacked on this
                // existing tick rather than a second timer thread -- both
                // saves skip the disk write entirely when nothing changed.
                RecordSessionPlaces();
                editor::SaveFilePlaces();
                SaveProjectSessionNow();
            });
        }
    });
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
        data.breakpoints = dapManager_->AllBreakpoints();
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
        default:
            return; // BufferView only ever forwards these five
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

void WindowManager::RebuildComponentTree() {
    // FTXUI -> Notcurses migration: was DetachAllChildren()+Add(... |
    // ApplyFlex) against a Vertical Container -- Layout.h's own Container
    // has no separate "flex the one child to fill the container" concept
    // to apply after the fact; SizeSpec::Flex() on the child itself (below)
    // is what does that directly, the same way BuildComponent's own
    // Split cases already give each of their two children a Flex weight.
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

    if (node->kind == WindowNode::Kind::SplitRight) {
        if (!node->divider) {
            node->divider = std::make_unique<VerticalDivider>(theme_);
        }
        if (!node->container) {
            node->container = std::make_unique<Container>(Axis::Horizontal, std::vector<Container::Child>{});
        }
        node->container->SetChildren({
            {&first, SizeSpec::Flex()},
            {node->divider.get(), SizeSpec::Fixed(1)},
            {&second, SizeSpec::Flex()},
        });
        return *node->container;
    }

    // SplitBelow -- the top pane's own ModeLine row already provides the
    // visual boundary, no separate divider needed (see this file's own
    // header comment).
    if (!node->container) {
        node->container = std::make_unique<Container>(Axis::Vertical, std::vector<Container::Child>{});
    }
    node->container->SetChildren({
        {&first, SizeSpec::Flex()},
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
