//
// Emacs-style window splitting (window-splitting follow-up): recursive
// horizontal/vertical splits of the buffer-editing area, each pane a fully
// independent BufferView with its own point/scroll/undo (already true
// per-Buffer), its own Dispatcher (a prefix-key sequence in progress
// genuinely belongs to whichever pane is receiving keystrokes -- sharing
// one Dispatcher across panes would let a prefix key started in one
// complete while focus is in another), its own ActiveBuffer, its own
// ModeLine, and its own scroll bar. C-x 2/3/0/1/o (split-below/split-right/
// delete-window/delete-other-windows/other-window) are the only new
// keybindings; see Editor/Commands.cpp.
//
// No hand-rolled "current window" pointer anywhere in here -- focus is
// derived on demand from Widget.h's own flat focus registry (Focused()/
// TakeFocus()/FocusedWidget()), the same "recompute, don't cache" convention
// every other per-frame sync in this codebase already uses.
//
// Fixed 50/50 splits only in this version -- no drag-to-resize yet, mirroring
// this project's own precedent (ProjectSidebar's drag-resize divider was
// explicitly a round-2 follow-up on top of an initial fixed-width v1).
//

#ifndef NED_UI_WINDOWMANAGER_H
#define NED_UI_WINDOWMANAGER_H

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ActiveBuffer.h"
#include "AsyncFileLoader.h"
#include "HugeFileLoader.h"
#include "BufferView.h"
#include "Editor/Command.h"
#include "Editor/Dispatcher.h"
#include "Editor/FileWatch.h"
#include "Editor/Keymap.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Mode.h"
#include "Editor/ProjectSession.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Tasks/TaskRunner.h"
#include "Editor/TestRun/TestRunner.h"
#include "Editor/Vcs/VcsRunner.h"
#include "EventLoop.h"
#include "Layout.h"
#include "Minimap.h"
#include "ModeLine.h"
#include "ProjectSidebar.h"
#include "VcsPanel.h"
#include "ScrollArrowButton.h"
#include "ScrollBar.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "Theme.h"

namespace ned::ui {

// One pane: a BufferView plus every piece of state that must be genuinely
// per-pane rather than shared app-wide (see this file's own header comment).
// Deliberately not its own file -- a private implementation detail of
// WindowManager, the same "small private nested type, not its own file"
// call TabBar.h already makes for its own TabLayout struct.
//
// Never moved or copied once constructed (explicitly deleted below): its own
// Dispatcher's KeymapStack holds a raw `const Keymap*` pointing at this
// same Pane's own mode_.keymap, the same self-referential-by-address
// relationship main.cpp's pre-window-splitting locals (dispatcher/mode as
// sibling stack variables) already had -- safe there because neither ever
// moved either. Pane is always heap-allocated via a stable std::unique_ptr
// (WindowNode::pane below), so this is a documented invariant, not a
// theoretical one.
class Pane {
  public:
    // buffer is the buffer this pane starts showing (the same buffer as the
    // pane it was split from, for a new pane -- see WindowManager::MakePane
    // -- or the app's initial buffer for the very first one). mode is moved
    // in as an owned copy, not a reference (window-splitting's own scope
    // decision -- see WindowManager.cpp's own comment on WHERE that copy is
    // taken from). killRing/registers/promptHistory/bufferList/registry/janetKeymap/
    // globalKeymap/statusMessage/theme are shared app-wide and must outlive
    // every Pane; projectSidebar/lspManager/janetEnv may be nullptr (not yet
    // wired up when the first Pane is constructed, see
    // WindowManager::SetProjectSidebar/SetLspManager/SetJanetEnvironment).
    // onWindowRequest/onBufferClosed mirror
    // BufferView::SetOnWindowRequest/SetOnBufferClosed exactly -- forwarded
    // straight through to the BufferView this Pane owns.
    Pane(text::Buffer& buffer, text::KillRing& killRing, editor::RegisterTable& registers,
         editor::PromptHistory& promptHistory, text::BufferList& bufferList, const editor::CommandRegistry& registry,
         const editor::Keymap& janetKeymap, const editor::Keymap& globalKeymap, editor::Mode mode,
         std::string& statusMessage, const Theme& theme,
         ProjectSidebar* projectSidebar, editor::lsp::LspManager* lspManager, editor::tasks::TaskRunner* taskRunner,
         editor::testrun::TestRunner* testRunner, editor::vcs::VcsRunner* vcsRunner, editor::dap::DapManager* dapManager,
         editor::acp::AcpManager* acpManager, editor::ProjectUndoManager* projectUndo, const janet::Environment* janetEnv,
         std::function<void(editor::InteractiveRequest)> onWindowRequest, std::function<void(text::Buffer&)> onBufferClosed);

    Pane(const Pane&)            = delete;
    Pane& operator=(const Pane&) = delete;
    Pane(Pane&&)                 = delete;
    Pane& operator=(Pane&&)      = delete;

    [[nodiscard]] ActiveBuffer&       ActiveBufferRef();
    [[nodiscard]] BufferView&         Buffer();
    [[nodiscard]] ModeLine&           ModeLineRef();
    [[nodiscard]] const editor::Mode& ModeRef() const;

    // Hands back a reference to this Pane's Layout.h Container (see
    // component_ below), owned directly by the Pane. Callers
    // (WindowManager::BuildComponent) hold this only as long as the owning
    // Pane does, the same lifetime contract every other Widget& in this
    // codebase already has.
    [[nodiscard]] Widget& Component();

    // Sets this pane's EventLoop -- forwarded straight to bufferView_ (see
    // BufferView::SetEventLoop) and to scrollUp_/scrollDown_ (their
    // press-and-hold repeat needs it too). Unset is a safe no-op, matching
    // every other Set* hook in this codebase.
    void SetEventLoop(EventLoop* eventLoop);

    // Minimap widget follow-up: test-only introspection points (mirrors
    // ScrollArrowButton::IsRepeating()'s own "expose a small, honest
    // introspection point rather than let tests reach into internals"
    // precedent) -- lets a test assert the lockstep-opposite invariant
    // (never both active, never both inactive) without reaching into
    // Container's own private children_.
    [[nodiscard]] bool MinimapActive() const;
    [[nodiscard]] bool ScrollColumnActive() const;

    // Pixel-blitter-minimap follow-up: tears down this pane's Minimap
    // pixel-blitter plane, if it has one live, while the owning EventLoop's
    // Notcurses context is still guaranteed alive -- see
    // WindowManager::ReleaseMinimapPixelPlanes()'s own doc comment for why
    // this can't just be left to ~Minimap().
    void ReleaseMinimapPixelPlane();

    // per-buffer-highlight-cache follow-up: erases buffer's entry from this
    // pane's own BufferView/Minimap per-buffer highlight/fold caches.
    // Called for every pane (not just one showing buffer right now) from
    // WindowManager::ReassignPanesShowing -- the shared close funnel -- so
    // a closed Buffer* can never linger as a stale cache key in a pane that
    // merely visited it in the past.
    void ClearBufferCaches(text::Buffer& buffer);

  private:
    ActiveBuffer                       activeBuffer_;
    editor::Mode                       mode_; // owned copy -- see the class comment above
    editor::Dispatcher                 dispatcher_;
    std::shared_ptr<BufferView>        bufferView_;
    std::shared_ptr<ModeLine>          modeLine_;
    std::shared_ptr<ScrollBar>         scrollBar_;
    std::shared_ptr<ScrollArrowButton> scrollUp_;
    std::shared_ptr<ScrollArrowButton> scrollDown_;
    std::shared_ptr<Minimap>           minimap_;

    // This pane's own precomposed subtree, built once at construction --
    // scrollColumn_ holds {scrollUp_, scrollBar_, scrollDown_}, row_ holds
    // {bufferView_, scrollColumn_, minimap_} (minimap widget follow-up:
    // scrollColumn_/minimap_ are kept as exact opposites via their own
    // Widget::active flags, seeded from editor::MinimapEnabled() at
    // construction and flipped in lockstep by toggle-minimap -- see
    // BufferView::SetMinimap's own doc comment -- so exactly one of the two
    // ever actually occupies space in row_'s layout, regardless of which),
    // component_ holds {row_, modeLine_}.
    // Declared in this order (children before the Containers that reference
    // them, which C++ requires nothing of structurally since these are all
    // separate objects linked by raw Widget* -- but member destruction
    // order still matters not at all here, since none of these ever
    // outlives any other within the same Pane).
    Container scrollColumn_;
    Container row_;
    Container component_;
};

// A recursive binary tree: a Leaf is one live Pane; a SplitBelow/SplitRight
// node is two children divided horizontally/vertically. Split-resize
// follow-up: ratio (below) replaced this file's own "fixed 50/50, no
// drag-to-resize yet" v1 note -- see WindowManager.cpp's SplitDivider for
// how it's read/written.
struct WindowNode {
    enum class Kind { Leaf,
                      SplitBelow,
                      SplitRight };

    Kind                        kind = Kind::Leaf;
    std::unique_ptr<Pane>       pane;          // Kind::Leaf
    std::unique_ptr<WindowNode> first, second; // Kind::SplitBelow / SplitRight

    // Split-resize follow-up: `first`'s fractional share of the split's main
    // axis (0..1); `second` always takes whatever's left. Read fresh every
    // Paint() by the DynamicFixed SizeSpec BuildComponent gives `first` --
    // not baked into `container`'s children_ at build time -- so a live
    // mouse-drag (SplitDivider::UpdateResize mutates this directly) or a
    // keyboard enlarge-/shrink-window command takes effect on the very next
    // frame with no rebuild. Unused for Kind::Leaf.
    float ratio = 0.5f;

    // A SplitBelow/SplitRight node needs somewhere stable to keep its
    // Layout.h Container across rebuilds -- this is that slot, (re)built by
    // WindowManager::BuildComponent every RebuildComponentTree() call
    // (SetChildren, not a fresh Container, so its own identity -- and thus
    // its Box_() -- survives a rebuild that doesn't touch this particular
    // node). Unused for Kind::Leaf.
    std::unique_ptr<Container> container;
    // The one-column vertical divider between a SplitRight's two children
    // (a SplitBelow needs none -- the top pane's own ModeLine row already
    // provides the visual boundary, see WindowManager.cpp's own comment).
    // Lives here, not as a free-standing local in BuildComponent, for the
    // same "needs a stable address across rebuilds" reason container does.
    // Unused for Kind::Leaf or Kind::SplitBelow.
    std::unique_ptr<Widget> divider;
};

class WindowManager {
  public:
    // initialBuffer is the app's own starting buffer; initialMode is the
    // Mode main.cpp already picks once at startup from that buffer's file
    // extension (unchanged from before window-splitting -- see this file's
    // header comment on the Mode-per-pane scope decision). killRing/
    // registers/promptHistory/bufferList/registry/janetKeymap/globalKeymap/statusMessage/
    // theme must outlive this WindowManager (the usual convention, matching
    // every other externally-owned reference already in this codebase).
    WindowManager(text::Buffer& initialBuffer, text::KillRing& killRing, editor::RegisterTable& registers,
                  editor::PromptHistory& promptHistory, text::BufferList& bufferList,
                  const editor::CommandRegistry& registry, const editor::Keymap& janetKeymap,
                  const editor::Keymap& globalKeymap, editor::Mode initialMode, std::string& statusMessage,
                  const Theme& theme);

    WindowManager(const WindowManager&)            = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    // Forwarded to every pane, present and future (new panes created by a
    // later split are wired up with whatever was registered here, even if
    // that happens after some panes already exist).
    void SetProjectSidebar(ProjectSidebar* sidebar);

    // VCS side panel: same "forwarded to every pane, present and future"
    // shape as SetProjectSidebar above -- also forwarded to projectSidebar_
    // (and vice versa) so each pane's BufferView can keep the two mutually
    // exclusive on the shared left dock slot; see
    // BufferView::SetVcsPanel's own doc comment.
    void SetVcsPanel(VcsPanel* panel);

    // LSP client follow-up: same "forwarded to every pane, present and
    // future" shape as SetProjectSidebar above. Also used by
    // HandleBufferClosed/NotifyBufferClosing to send textDocument/didClose
    // for a real buffer close, regardless of which pane (or ProjectSidebar's
    // preview-swap, which isn't pane-driven at all) triggered it.
    void SetLspManager(editor::lsp::LspManager* lspManager);

    // rich-theme-set follow-up (Phase 1): same "forwarded to every pane,
    // present and future" shape as SetProjectSidebar/SetLspManager -- the
    // select-theme picker runs in whichever pane has focus, so every pane's
    // BufferView needs the applier (see BufferView::SetThemeApplier's own
    // doc comment for what main.cpp wires in).
    void SetThemeApplier(std::function<void(const Theme&)> applier);

    // terminal-panel follow-up: same "forwarded to every pane, present and
    // future" shape as SetThemeApplier above -- toggle-terminal can fire
    // from whichever pane has focus, and the handler (main.cpp's
    // three-state toggle over the OverlayHost-owned TerminalPanel) lives
    // above this class entirely.
    void SetOnTerminalToggle(std::function<void()> onToggle);

    // ACP chat panel: same "forwarded to every pane, present and future"
    // shape as SetOnTerminalToggle immediately above -- acp-toggle-panel can
    // fire from whichever pane has focus, and the handler (main.cpp's toggle
    // over the OverlayHost-owned AcpPanel) lives above this class entirely.
    void SetOnAcpPanelToggle(std::function<void()> onToggle);

    // ACP round-1-live-validation follow-up: lets SetAcpManager's own
    // SetOnPermissionRequest wiring below know whether the OverlayHost-owned
    // AcpPanel (main.cpp's, not owned here either) currently has focus --
    // when it does, AcpPanel::OnEvent resolves the prompt itself (it already
    // renders it read-only via AcpManager::PendingPermissionPrompt()), so the
    // existing focused-pane echo-area routing is skipped rather than fighting
    // over the same request. Unset is a safe no-op (checker treated as
    // "always false"), same convention as every other Set* hook -- existing
    // pane-routing behavior is unchanged until main.cpp wires this.
    void SetAcpPanelFocusChecker(std::function<bool()> checker);

    // DAP round 2: same "forwarded to every pane, present and future" shape
    // as SetOnAcpPanelToggle immediately above -- dap-toggle-console can
    // fire from whichever pane has focus, and the handler (main.cpp's
    // toggle over the OverlayHost-owned DebugConsolePanel) lives above this
    // class entirely.
    void SetOnDapConsoleToggle(std::function<void()> onToggle);

    // generic-popup follow-up: same "forwarded to every pane, present and
    // future" shape as SetOnDapConsoleToggle immediately above --
    // list-buffers can fire from whichever pane has focus, and the handler
    // (main.cpp's toggle over the OverlayHost-owned BufferListPanel) lives
    // above this class entirely.
    void SetOnBufferListToggle(std::function<void()> onToggle);

    // which-key follow-up: same "forwarded to every pane, present and
    // future" shape as SetOnDapConsoleToggle above, but a data callback
    // fired on every Pending/non-Pending transition rather than an explicit
    // toggle -- see BufferView::SetOnPrefixHintChanged's own doc comment.
    void SetOnPrefixHintChanged(std::function<void(std::optional<WhichKeyHint>)> onHintChanged);

    // generic-popup follow-up (Phase 3): same "forwarded to every pane,
    // present and future" shape as SetOnPrefixHintChanged immediately
    // above -- see BufferView::SetOnCandidatesChanged's own doc comment.
    void SetOnCandidatesChanged(std::function<void(std::optional<ListPopupModel>)> onCandidatesChanged);

    // completion-popup follow-up: same "forwarded to every pane, present and
    // future" shape as SetOnCandidatesChanged immediately above -- see
    // BufferView::SetOnCompletionChanged's own doc comment for why this is
    // a distinct hook rather than a reuse of that one.
    void SetOnCompletionChanged(std::function<void(std::optional<ListPopupModel>)> onCompletionChanged);

    // call/type-hierarchy follow-up: NOT a plain "forwarded to every pane"
    // shape the way SetOnCandidatesChanged/SetOnCompletionChanged above
    // are -- each pane's BufferView is wrapped in its own small forwarding
    // lambda (WireHierarchyCallback) that first records which Pane just
    // showed/hid the shared TreeView overlay (hierarchyOwnerPane_) before
    // calling the handler main.cpp gave here. That's what lets
    // HierarchyActivate/HierarchyToggleExpand/HierarchyCollapse/
    // HierarchyCancel/HierarchySelectionChanged below route back to the
    // right BufferView at all: once the TreeView overlay holds keyboard
    // focus, the originating pane's own BufferView is no longer Focused()
    // (see FocusedPane()'s own definition), so the "route to whichever pane
    // is currently focused" shape ActivateCompletionAt/TriggerSwitchProject
    // use doesn't work for this session -- see BufferView::
    // SetOnHierarchyChanged's own doc comment.
    void SetOnHierarchyChanged(std::function<void(std::optional<TreeViewModel>)> onHierarchyChanged);

    // Routes to hierarchyOwnerPane_ (see SetOnHierarchyChanged above), not
    // FocusedPane() -- a no-op if no session is currently visible anywhere.
    // Wired to the shared TreeView overlay's own SetOnActivate/
    // SetOnToggleExpand/SetOnCollapseRequested/SetOnCancel/
    // SetOnSelectionChanged in main.cpp.
    void HierarchyActivate(std::size_t index);
    void HierarchyToggleExpand(std::size_t index);
    void HierarchyCollapse(std::size_t index);
    void HierarchyCancel();
    void HierarchySelectionChanged(std::size_t index);

    // task-runner follow-up: same "forwarded to every pane, present and
    // future" shape as SetProjectSidebar/SetLspManager above.
    void SetTaskRunner(editor::tasks::TaskRunner* taskRunner);

    // test-runner integration: same forwarded-to-every-pane shape.
    void SetTestRunner(editor::testrun::TestRunner* testRunner);

    // Minimap widget follow-up: test-only introspection point, same
    // "expose a small, honest introspection point" precedent as
    // ScrollArrowButton::IsRepeating() -- reports the focused pane's own
    // Pane::MinimapActive()/ScrollColumnActive(), or false if no pane is
    // currently focused (mirrors FocusedActiveBuffer()'s own "derived
    // fresh every call, never cached" convention).
    [[nodiscard]] bool FocusedPaneMinimapActive();
    [[nodiscard]] bool FocusedPaneScrollColumnActive();

    // Chrome-focus follow-up: whether any pane's BufferView currently holds
    // the keyboard focus -- i.e. the keyboard is "in the editor" rather
    // than, say, the project sidebar. TabBar's underline accent keys off
    // this (see main.cpp's wiring), derived fresh every call like the two
    // queries above.
    [[nodiscard]] bool HasFocusedPane();

    void SetVcsRunner(editor::vcs::VcsRunner* vcsRunner);

    // DAP client slice 1: same "forwarded to every pane, present and
    // future" shape as SetLspManager/SetTaskRunner above -- plus this is
    // where the session's async callbacks (SetOnStopped/SetOnSessionEnded)
    // get wired, since WindowManager is the one owner that can resolve
    // "the focused pane" fresh when a breakpoint actually fires, rather
    // than capturing some pane that may have been closed by then.
    void SetDapManager(editor::dap::DapManager* dapManager);

    // ACP client slice 2: same "forwarded to every pane, present and
    // future" shape as SetDapManager above -- plus this is where
    // SetOnPermissionRequest/SetOnSessionEnded get wired, since
    // WindowManager is the one owner that can resolve "the focused pane"
    // fresh when a permission request actually arrives, rather than
    // capturing some pane that may have been closed by then (identical
    // reasoning to SetDapManager's own SetOnStopped wiring).
    void SetAcpManager(editor::acp::AcpManager* acpManager);

    // ACP auto-reconnect follow-up: seeds SaveProjectSessionNow's own
    // lastAcpAgent field for a run where AcpManager::AgentName() never gets
    // set at all (ACP never touched this run) -- without this, a fresh
    // ProjectSessionData built from scratch every save would otherwise
    // silently clobber a previously remembered agent name back to nullopt.
    // Called once by main.cpp right after loading the restored session, with
    // whatever it found there (or nothing, if this is a project's first-ever
    // session).
    void SetLastKnownAcpAgent(std::optional<std::string> name);

    // project-undo follow-up: same "forwarded to every pane, present and
    // future" shape as SetLspManager/SetTaskRunner above.
    void SetProjectUndo(editor::ProjectUndoManager* projectUndo);

    // Self-hosting-completion follow-up: same "forwarded to every pane,
    // present and future" shape as SetLspManager above -- see
    // BufferView::SetJanetEnvironment's own doc comment.
    void SetJanetEnvironment(const janet::Environment* janetEnv);

    // Forwarded to every pane, present and future, same shape as
    // SetProjectSidebar/SetLspManager above -- see Pane::SetEventLoop's own
    // doc comment.
    void SetEventLoop(EventLoop* eventLoop);

    // One stable Widget& main.cpp embeds exactly once into its own
    // composition root and never needs to re-fetch -- its own children get
    // swapped out on every split/close (Container::SetChildren), but its own
    // identity never changes, the same "long-lived mutable slot" role
    // main.cpp's own active-flagged ProjectSidebar already plays for a
    // different reason (conditional visibility rather than structural
    // rebuilds).
    [[nodiscard]] Widget& RootComponent();

    // Re-establishes keyboard focus on whichever pane currently has it
    // (the initial one, unless something else has already changed focus
    // before this is called), falling back to the first leaf if no pane
    // reports Focused() yet. main.cpp calls this once, after RootComponent()
    // has been embedded into the app's full composition tree.
    void TakeFocus();

    // How many panes currently exist -- mainly for tests (asserting on tree
    // shape after a split/close), the same "expose a small, honest
    // introspection point rather than let tests reach into internals" call
    // ScrollArrowButton::IsRepeating() already makes for a comparable
    // real-elapsed-time-would-be-flaky situation.
    [[nodiscard]] std::size_t WindowCount() const;

    // The ActiveBuffer belonging to whichever pane currently has keyboard
    // focus -- what TabBar/ProjectSidebar's own activeBufferProvider
    // callbacks resolve to (see their own header comments). Derived fresh
    // every call by walking Leaves() and testing Focused(), never cached.
    [[nodiscard]] ActiveBuffer& FocusedActiveBuffer();

    // Routes to whichever pane is currently focused -- the interactive y/n
    // confirmation session (for a modified buffer) still legitimately runs
    // on that pane's own BufferView, unchanged; this is just the new entry
    // point TabBar's close-icon click retargets to instead of a single
    // fixed BufferView.
    void RequestCloseBuffer(text::Buffer& buffer);

    // open-binary-anyway follow-up: same "route to whichever pane is
    // currently focused" shape as RequestCloseBuffer just above -- wired to
    // ProjectSidebar::SetOnBinaryFileOpenRequest so a sidebar click on a
    // binary file gets the same y/n confirmation find-file's own
    // HandlePromptKey already offers, instead of just reporting a refusal.
    void RequestOpenBinaryFile(const std::filesystem::path& path);

    // VCS side panel: same "route to whichever pane is currently focused"
    // shape as RequestOpenBinaryFile just above -- wired to
    // VcsPanel::SetOnAction so a panel-triggered commit/branch-switch/
    // branch-create starts on the focused pane's own BufferView.
    void RequestVcsPanelAction(VcsPanelAction action);

    // named-projects follow-up: same "route to whichever pane is currently
    // focused" shape as RequestOpenBinaryFile just above -- wired to
    // ProjectSidebar::SetOnHeaderClicked so a click on the sidebar's title
    // row opens the switch-project picker.
    void TriggerSwitchProject();

    // mouse-support follow-up: same "route to whichever pane is currently
    // focused" shape as RequestCloseBuffer/RequestOpenBinaryFile above --
    // wired to the completion popup's own ListPopup::SetOnActivate in
    // main.cpp, so a click on a completion row accepts it the same way
    // Tab does. See BufferView::AcceptActiveCompletionAt's own doc comment.
    void ActivateCompletionAt(std::size_t index);

    // session-persistence slice 3: routes the .ned/init.janet trust prompt
    // to whichever pane has focus -- RequestOpenBinaryFile's exact shape;
    // see BufferView::RequestTrustProjectInit for the prompt's own
    // contract. main.cpp calls this once at startup, after TakeFocus, for
    // an existing-but-untrusted project init file.
    void RequestTrustProjectInit(
        const std::filesystem::path&                                                   initPath,
        std::function<void(const std::filesystem::path&, editor::ProjectInitDecision)> onDecision);

    // Called whenever a buffer is about to be closed by a path that doesn't
    // go through BufferView::CloseBufferNow at all -- currently just
    // ProjectSidebar::OpenFileEntry, which closes the outgoing single-
    // click-preview buffer directly (bufferList_.Close(...)) with no
    // self-reassignment step of its own for any pane, unlike
    // CloseBufferNow's own callers. Reassigns *every* pane currently
    // showing closingBuffer (no exceptions -- unlike the private
    // HandleBufferClosed, nothing else handles any one pane specially
    // here), sharing a single fresh scratch buffer across all of them if
    // there's genuinely nothing else open. Must be called before the
    // buffer is actually erased -- see ProjectSidebar::SetOnBufferClosed's
    // own doc comment for why skipping this was a real, confirmed
    // dangling-reference crash (heap corruption manifesting in
    // ModeLine::Paint), not a hypothetical one.
    void NotifyBufferClosing(text::Buffer& closingBuffer);

    // Starts the periodic scratch auto-save timer -- moved here, verbatim,
    // from BufferView (window-splitting follow-up): the background thread
    // only ever touches bufferList_, never any one pane's own state, so it
    // never had genuine per-pane affinity: its previous home on BufferView
    // was fine only because there used to be exactly one. Left there, a
    // pane closed via delete-window could silently end autosave for the
    // rest of the session -- WindowManager is the actual whole-session-
    // lifetime owner autosave semantically wants. Not started automatically
    // at construction, for the same "don't spin up a real thread in every
    // test" reason BufferView's own version never was; main.cpp calls this
    // once, for the real running editor only.
    void StartAutoSaveTimer(EventLoop& eventLoop);

    // file-watcher follow-up: constructs fileWatcher_ (Editor/FileWatch.h,
    // inotify-backed) so an external write underneath an open buffer fires
    // SweepExternalChanges near-instantly instead of waiting for the tick
    // above -- which keeps running unchanged as the safety net for anything
    // inotify can't see (NFS, watch-budget exhaustion). Constructed even
    // when ned/set-file-watch is currently off (init.janet runs before this
    // is called, and gating construction on the toggle would make a later
    // re-enable dead) -- ResyncFileWatcher just keeps the watch set empty
    // while disabled. Not called from the constructor, for the same "don't
    // spin up a real thread in every test" reason StartAutoSaveTimer isn't;
    // main.cpp calls this once, alongside it, for the real running editor.
    // Note ned's own saves fire the watcher too -- harmless: the save
    // already advanced the buffer's stored disk timestamp, so the resulting
    // sweep is a cheap no-op.
    void StartFileWatcher(EventLoop& eventLoop);

    // vcs-diff-gutter-staleness follow-up: refreshes every live pane's own
    // diff gutter (BufferView::RefreshVcsDiff, silently no-op with no
    // VcsRunner wired) -- the gutter's freshness is otherwise purely
    // event-driven off things ned itself did (an edit, a buffer switch, a
    // save, ned's own vcs-commit/stage/unstage), so a commit/checkout run
    // from outside ned (another terminal, or the embedded TerminalPanel)
    // previously left it stale indefinitely. Called from two places: the
    // auto-save timer's own tick below (a periodic catch-all for any
    // external source) and main.cpp's toggle-terminal closing edge (an
    // instant refresh right when the embedded terminal -- the most likely
    // place to run a git command from inside ned -- closes).
    void RefreshVcsDiffGutters();

    // session-persistence slice 1: records every open file buffer's current
    // place (Editor/Session.h) -- all of bufferList_ first with no viewport
    // information, then each live pane's own active buffer again with its
    // real TopLine(), so visible buffers' entries carry the viewport and
    // background buffers' entries keep whatever topLine they last had.
    // Called from the auto-save timer tick (alongside
    // AutoSaveScratchBuffers, same unattended-swallow posture) and once by
    // main.cpp after the event loop exits; deliberately not from any
    // buffer-switch seam -- the old-buffer pointer available there can
    // already be dangling mid-close (see NotifyBufferClosing above), and a
    // <=5s-stale record is an accepted trade, not a bug.
    void RecordSessionPlaces();

    // session-persistence slice 2: captures the current project session
    // (open file buffers -- skipping the transient PreviewBuffer() and
    // scratch-pad buffers, which are global, not project state -- the
    // focused pane's active file, sidebar visibility/width, DAP
    // breakpoints) and hands it to editor::SaveActiveProjectSession, which
    // itself no-ops unless main.cpp established a real project root for
    // this run. Called from the same auto-save tick as RecordSessionPlaces
    // and once by main.cpp after the event loop exits.
    void SaveProjectSessionNow();

    // session-persistence-window-layout follow-up: fills in data.windowLayout
    // (and data.focusedPanePath) by walking root_ -- called from
    // SaveProjectSessionNow alongside the fields above. Leaves both empty,
    // untouched, if any leaf's buffer has no path (a scratch buffer showing
    // in a pane, say) rather than capture a tree partial restore couldn't
    // fully resolve anyway.
    void CaptureWindowLayout(editor::ProjectSessionData& data);

    // Rebuilds root_ from data.windowLayout, replacing whatever single
    // default pane main.cpp's constructor call already established, then
    // restores focus per data.focusedPanePath. A no-op (leaves the existing
    // root_ alone) when windowLayout is empty, or when any referenced file
    // isn't already open in bufferList_ -- main.cpp is expected to have
    // opened every ProjectSessionData::openFiles entry first, the same
    // ordering LoadActiveProjectSession's other restore steps already rely
    // on.
    void RestoreWindowLayout(const editor::ProjectSessionData& data);

    // Pixel-blitter-minimap follow-up: tears down every pane's Minimap
    // pixel-blitter plane while the Notcurses context is still guaranteed
    // alive. main.cpp's local-variable order means this WindowManager is
    // destroyed *after* ~EventLoop already ran notcurses_stop (a
    // pre-existing, deliberate ordering elsewhere in that file) -- a
    // Minimap torn down that late would call ncplane_destroy on memory
    // Notcurses already freed, a real, confirmed SIGABRT-on-exit, not a
    // hypothetical one. Called once by main.cpp immediately after
    // eventLoop.Run() returns, same "explicit final step, everything's
    // still alive here" shape as RecordSessionPlaces/SaveProjectSessionNow
    // just above.
    void ReleaseMinimapPixelPlanes();

    // large-file-async-load follow-up: wires bufferList_.SetAsyncFileOpener
    // to spin up an AsyncFileLoader (Source/UI/AsyncFileLoader.h) per large
    // file open, owned here in asyncFileLoaders_ for as long as it's still
    // in flight. Not called from the constructor, for the same "don't
    // require a live EventLoop in every test that constructs a
    // WindowManager" reason StartAutoSaveTimer isn't either; main.cpp calls
    // this once, alongside StartAutoSaveTimer, for the real running editor.
    void EnableAsyncFileLoading(EventLoop& eventLoop);

    // progressive-huge-file-load follow-up: the huge-tier counterpart --
    // wires bufferList_.SetAsyncHugeFileOpener to spin up a HugeFileLoader
    // (Source/UI/HugeFileLoader.h) per huge file open instead of the
    // AsyncFileLoader above, owned here in hugeFileLoaders_. Same
    // not-called-from-the-constructor reasoning as EnableAsyncFileLoading;
    // main.cpp calls both once, alongside StartAutoSaveTimer.
    void EnableAsyncHugeFileLoading(EventLoop& eventLoop);

  private:
    // Drops any asyncFileLoaders_ entries that have finished (AsyncFileLoader
    // ::Done()) -- called opportunistically whenever a new load starts,
    // rather than on a separate timer; there are only ever as many entries
    // as there are large files currently being opened at once, so an
    // unbounded-until-next-open backlog is not a real concern.
    void PurgeFinishedAsyncLoaders();
    // Same as PurgeFinishedAsyncLoaders, for hugeFileLoaders_/HugeFileLoader.
    void PurgeFinishedHugeFileLoaders();

    // file-watcher follow-up: the external-change portion of the auto-save
    // tick (AutoRevertBuffers + AutoMergeBuffers + their status-line
    // messages, then RefreshVcsDiffGutters -- a disk change stales the diff
    // gutter too), factored out verbatim so the inotify watcher's Posted
    // callback and the periodic tick run the exact same sweep. Always runs
    // on the main thread (called from Posted lambdas only). Idempotent:
    // both triggers re-check Buffer::ExternallyModified per buffer, so a
    // watcher-fired sweep racing the tick's own is just a cheap no-op.
    void SweepExternalChanges();

    // file-watcher follow-up: re-derives fileWatcher_'s watch set from
    // bufferList_.Buffers() (every path-backed buffer), or clears it when
    // ned/set-file-watch is off. Called from StartFileWatcher, from each
    // auto-save tick (so a newly opened buffer is watched within <=5s --
    // deliberately no buffer-open hook: BufferList::SetOnFileOpened is
    // single-slot and already claimed, and the poll sweep covers the gap),
    // and after each watcher-fired sweep (a sweep can revert a buffer whose
    // file was replaced, and buffers close). No-op before StartFileWatcher.
    void ResyncFileWatcher();

    [[nodiscard]] std::unique_ptr<Pane> MakePane(text::Buffer& buffer, editor::Mode mode);

    // RestoreWindowLayout's own recursive builder -- nullptr on any
    // unresolvable reference (out-of-range/self-or-forward-referencing
    // index, a Leaf whose file isn't open in bufferList_), which
    // RestoreWindowLayout treats as "abandon the whole restore."
    [[nodiscard]] std::unique_ptr<WindowNode> BuildNodeFromLayout(const std::vector<editor::WindowLayoutNode>& nodes, std::size_t index);

    void HandleWindowRequest(editor::InteractiveRequest request);
    void HandleBufferClosed(text::Buffer& closedBuffer);

    // Shared by HandleBufferClosed and the public NotifyBufferClosing --
    // reassigns every pane (other than `skip`, if non-null) currently
    // showing closingBuffer to some other live buffer, conjuring a single
    // shared fresh scratch buffer if there's genuinely nothing else open.
    void ReassignPanesShowing(text::Buffer& closingBuffer, Pane* skip);

    void DoSplit(WindowNode::Kind kind);
    void SplitBelow();
    void SplitRight();
    void DeleteWindow();
    void DeleteOtherWindows();
    void OtherWindow();

    // Split-resize follow-up: the keyboard half of split-pane resize
    // (enlarge-window/shrink-window/-horizontally). axis selects which
    // ancestor split kind is eligible (SplitBelow for the plain pair,
    // SplitRight for the -horizontally pair); grow true nudges the ratio so
    // the focused pane's own side gets bigger, false shrinks it. A no-op if
    // the focused pane has no ancestor split of the requested axis (e.g.
    // enlarge-window in a single, unsplit window).
    void ResizeFocusedWindow(WindowNode::Kind axis, bool grow);

    // Rebuilds rootComponent_'s children from the current root_ tree shape
    // -- called after every structural mutation (split/close). Does NOT by
    // itself restore focus -- callers must explicitly TakeFocus()
    // afterward, since RebuildComponentTree can reparent/replace Containers
    // without touching which Widget the global focus registry (Widget.cpp)
    // currently points at, which could easily no longer be part of the tree
    // at all after a DeleteWindow.
    void RebuildComponentTree();

    // (Re)builds/updates node's own Widget subtree in place, recursing into
    // first/second first -- returns the Widget& to embed into node's own
    // parent. A Leaf just returns its Pane's own Component(); a Split
    // (re)builds node->container (and, for SplitRight, node->divider) via
    // SetChildren rather than constructing a fresh Container every call,
    // so a node whose own subtree structure hasn't changed keeps the exact
    // same Container identity (and thus the exact same already-assigned
    // Box_(), a real concern the very next Paint() would otherwise recompute
    // fresh anyway, but keeping identity stable costs nothing extra and
    // avoids reasoning about it).
    [[nodiscard]] Widget& BuildComponent(WindowNode* node) const;

    [[nodiscard]] Pane*              FocusedPane();
    [[nodiscard]] std::vector<Pane*> Leaves() const;

    text::KillRing&                   killRing_;
    editor::RegisterTable&            registers_;
    editor::PromptHistory&            promptHistory_;
    text::BufferList&                 bufferList_;
    const editor::CommandRegistry&    registry_;
    const editor::Keymap&             janetKeymap_;
    const editor::Keymap&             globalKeymap_;
    std::string&                      statusMessage_;
    const Theme&                      theme_;
    ProjectSidebar*                   projectSidebar_ = nullptr;
    VcsPanel*                         vcsPanel_       = nullptr;
    editor::lsp::LspManager*          lspManager_     = nullptr;
    editor::tasks::TaskRunner*        taskRunner_     = nullptr;
    editor::testrun::TestRunner*      testRunner_     = nullptr; // see SetTestRunner
    editor::vcs::VcsRunner*           vcsRunner_      = nullptr;
    editor::dap::DapManager*          dapManager_     = nullptr; // see SetDapManager
    editor::acp::AcpManager*          acpManager_     = nullptr; // see SetAcpManager
    std::optional<std::string>        lastAcpAgentSeed_;         // see SetLastKnownAcpAgent
    editor::ProjectUndoManager*       projectUndo_    = nullptr; // see SetProjectUndo
    const janet::Environment*         janetEnv_       = nullptr; // see SetJanetEnvironment
    EventLoop*                        eventLoop_      = nullptr; // see SetEventLoop
    std::function<void(const Theme&)> themeApplier_;             // see SetThemeApplier
    std::function<void()>             onTerminalToggle_;         // see SetOnTerminalToggle
    std::function<void()>             onAcpPanelToggle_;         // see SetOnAcpPanelToggle
    std::function<bool()>             acpPanelFocused_;          // see SetAcpPanelFocusChecker
    std::function<void()>             onDapConsoleToggle_;       // see SetOnDapConsoleToggle
    std::function<void()>             onBufferListToggle_;       // see SetOnBufferListToggle
    std::function<void(std::optional<WhichKeyHint>)> onPrefixHintChanged_; // see SetOnPrefixHintChanged
    std::function<void(std::optional<ListPopupModel>)> onCandidatesChanged_; // see SetOnCandidatesChanged
    std::function<void(std::optional<ListPopupModel>)> onCompletionChanged_; // see SetOnCompletionChanged

    // call/type-hierarchy follow-up: onHierarchyChanged_ is the handler
    // main.cpp gave SetOnHierarchyChanged, called from inside each pane's
    // own per-pane wrapper (WireHierarchyCallback) rather than forwarded
    // directly the way onCandidatesChanged_ is. hierarchyOwnerPane_ is
    // that wrapper's own bookkeeping: whichever Pane most recently showed
    // (non-nullopt model) the shared overlay, cleared back to nullptr the
    // moment any pane hides it (nullopt model) -- see SetOnHierarchyChanged's
    // own doc comment for why FocusedPane() can't serve this role instead.
    std::function<void(std::optional<TreeViewModel>)> onHierarchyChanged_;
    Pane*                                              hierarchyOwnerPane_ = nullptr;

    // Builds the per-pane wrapper SetOnHierarchyChanged/MakePane both need
    // -- factored out so the two call sites can't drift apart.
    [[nodiscard]] std::function<void(std::optional<TreeViewModel>)> WireHierarchyCallback(Pane* pane);

    std::unique_ptr<WindowNode> root_;
    Container                   rootComponent_{Axis::Vertical, {}};

    // Split-resize follow-up: true while any live SplitDivider (anywhere in
    // root_) is mid-drag -- set/cleared via the callback BuildComponent
    // wires into each divider at construction. Every pane's own BufferView
    // (SetSplitResizeQuery, wired in MakePane) consults this so a drag that
    // strays outside its own divider's column/row doesn't get misread as a
    // text-selection drag by whichever pane the cursor currently sits over.
    // Only one drag can ever be live at a time (one mouse), so a single flag
    // -- not one per divider -- is enough, the same "one resizing_ bool
    // suffices" precedent ProjectSidebar's own single instance already set.
    // mutable: set from the onResizingChanged callback BuildComponent wires
    // into each divider, and BuildComponent itself is const (it only ever
    // mutates WindowNode fields reached through its `node` parameter, never
    // WindowManager's own state directly -- this callback is the one
    // exception, transient UI drag state that doesn't change what
    // BuildComponent itself observably builds).
    mutable bool resizingSplit_ = false;

    // See StartAutoSaveTimer's own comment above.
    std::jthread autoSaveThread_;

    // See EnableAsyncFileLoading's own comment above.
    std::vector<std::unique_ptr<AsyncFileLoader>> asyncFileLoaders_;
    // See EnableAsyncHugeFileLoading's own comment above.
    std::vector<std::unique_ptr<HugeFileLoader>> hugeFileLoaders_;

    // See StartFileWatcher's own comment above. Shares autoSaveThread_'s
    // accepted latent shutdown ordering: main.cpp declares eventLoop after
    // windowManager, so ~EventLoop (which discards queued posts) runs
    // before this watcher is destroyed -- a post landing in that window is
    // dropped, never dispatched against dead state.
    std::unique_ptr<editor::FileWatcher> fileWatcher_;
};

} // namespace ned::ui

#endif // NED_UI_WINDOWMANAGER_H
