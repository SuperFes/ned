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
// derived on demand from FTXUI's own real ComponentBase::Focused()/
// TakeFocus() machinery (confirmed, not assumed, that BufferView::OnKeyEvent
// unconditionally returns true for any translatable key chord, so FTXUI's
// own container-level Tab/arrow-key focus-stealing can never fire underneath
// a focused BufferView), the same "recompute, don't cache" convention every
// other per-frame sync in this codebase already uses.
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
#include <thread>
#include <vector>

#include "ActiveBuffer.h"
#include "BufferView.h"
#include "Editor/Command.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Mode.h"
#include "Editor/Register.h"
#include "EventLoop.h"
#include "Layout.h"
#include "ModeLine.h"
#include "ProjectSidebar.h"
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
    // taken from). killRing/registers/bufferList/registry/janetKeymap/
    // globalKeymap/statusMessage/theme are shared app-wide and must outlive
    // every Pane; projectSidebar/lspManager may be nullptr (not yet wired up
    // when the first Pane is constructed, see
    // WindowManager::SetProjectSidebar/SetLspManager).
    // onWindowRequest/onBufferClosed mirror
    // BufferView::SetOnWindowRequest/SetOnBufferClosed exactly -- forwarded
    // straight through to the BufferView this Pane owns.
    Pane(text::Buffer& buffer, text::KillRing& killRing, editor::RegisterTable& registers,
         text::BufferList& bufferList, const editor::CommandRegistry& registry, const editor::Keymap& janetKeymap,
         const editor::Keymap& globalKeymap, editor::Mode mode, std::string& statusMessage, const Theme& theme,
         ProjectSidebar* projectSidebar, editor::lsp::LspManager* lspManager,
         std::function<void(editor::InteractiveRequest)> onWindowRequest,
         std::function<void(text::Buffer&)>              onBufferClosed);

    Pane(const Pane&)            = delete;
    Pane& operator=(const Pane&) = delete;
    Pane(Pane&&)                 = delete;
    Pane& operator=(Pane&&)      = delete;

    [[nodiscard]] ActiveBuffer&       ActiveBufferRef();
    [[nodiscard]] BufferView&         Buffer();
    [[nodiscard]] const editor::Mode& ModeRef() const;

    // FTXUI -> Notcurses migration: was Component() returning a shared,
    // reference-counted ftxui::Component -- Layout.h's own Container is a
    // plain Widget owned directly by this Pane instead (see component_
    // below), so this just hands back a reference to it. Callers
    // (WindowManager::BuildComponent) hold this only as long as the owning
    // Pane does, the same lifetime contract every other Widget& in this
    // codebase already has.
    [[nodiscard]] Widget& Component();

    // Sets this pane's EventLoop -- forwarded straight to bufferView_ (see
    // BufferView::SetEventLoop) and to scrollUp_/scrollDown_ (their
    // press-and-hold repeat needs it too). Unset is a safe no-op, matching
    // every other Set* hook in this codebase.
    void SetEventLoop(EventLoop* eventLoop);

  private:
    ActiveBuffer                       activeBuffer_;
    editor::Mode                       mode_; // owned copy -- see the class comment above
    editor::Dispatcher                 dispatcher_;
    std::shared_ptr<BufferView>        bufferView_;
    std::shared_ptr<ModeLine>          modeLine_;
    std::shared_ptr<ScrollBar>         scrollBar_;
    std::shared_ptr<ScrollArrowButton> scrollUp_;
    std::shared_ptr<ScrollArrowButton> scrollDown_;

    // This pane's own precomposed subtree, built once at construction --
    // scrollColumn_ holds {scrollUp_, scrollBar_, scrollDown_}, row_ holds
    // {bufferView_, scrollColumn_}, component_ holds {row_, modeLine_}.
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
// node is two children divided horizontally/vertically. No ratio field yet
// -- see this file's own header comment on why fixed 50/50 is the v1 choice.
struct WindowNode {
    enum class Kind { Leaf,
                      SplitBelow,
                      SplitRight };

    Kind                        kind = Kind::Leaf;
    std::unique_ptr<Pane>       pane;          // Kind::Leaf
    std::unique_ptr<WindowNode> first, second; // Kind::SplitBelow / SplitRight

    // FTXUI -> Notcurses migration: FTXUI's own Container::Horizontal/
    // Vertical calls used to be built fresh, ephemerally, inside
    // BuildComponent every single RebuildComponentTree() call (cheap
    // shared_ptr churn under FTXUI's own reference-counted Component
    // model). Layout.h's Container is a plain owned Widget instead, so a
    // SplitBelow/SplitRight node needs somewhere stable to actually keep
    // one across rebuilds -- this is that slot, (re)built by
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
    // registers/bufferList/registry/janetKeymap/globalKeymap/statusMessage/
    // theme must outlive this WindowManager (the usual convention, matching
    // every other externally-owned reference already in this codebase).
    WindowManager(text::Buffer& initialBuffer, text::KillRing& killRing, editor::RegisterTable& registers,
                  text::BufferList& bufferList, const editor::CommandRegistry& registry,
                  const editor::Keymap& janetKeymap, const editor::Keymap& globalKeymap, editor::Mode initialMode,
                  std::string& statusMessage,
                  const Theme& theme);

    WindowManager(const WindowManager&)            = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    // Forwarded to every pane, present and future (new panes created by a
    // later split are wired up with whatever was registered here, even if
    // that happens after some panes already exist).
    void SetProjectSidebar(ProjectSidebar* sidebar);

    // LSP client follow-up: same "forwarded to every pane, present and
    // future" shape as SetProjectSidebar above. Also used by
    // HandleBufferClosed/NotifyBufferClosing to send textDocument/didClose
    // for a real buffer close, regardless of which pane (or ProjectSidebar's
    // preview-swap, which isn't pane-driven at all) triggered it.
    void SetLspManager(editor::lsp::LspManager* lspManager);

    // FTXUI -> Notcurses migration: forwarded to every pane, present and
    // future, same shape as SetProjectSidebar/SetLspManager above -- see
    // Pane::SetEventLoop's own doc comment.
    void SetEventLoop(EventLoop* eventLoop);

    // One stable Widget& main.cpp embeds exactly once into its own
    // composition root and never needs to re-fetch -- its own children get
    // swapped out on every split/close (Container::SetChildren, was FTXUI's
    // DetachAllChildren + Add), but its own identity never changes, the
    // same "long-lived mutable slot" role main.cpp's own active-flagged
    // ProjectSidebar already plays for a different reason (conditional
    // visibility rather than structural rebuilds).
    [[nodiscard]] Widget& RootComponent();

    // Re-establishes keyboard focus on whichever pane currently has it
    // (the initial one, unless something else has already changed focus
    // before this is called). Must be called by main.cpp once, after
    // RootComponent() has actually been embedded into the app's full
    // composition tree -- NOT relied upon from this class's own
    // constructor, even though the initial pane's own TakeFocus() is
    // called there too. Real, confirmed reason (found via manual pty
    // testing, not guessed): ComponentBase::TakeFocus() walks UP through
    // real parent pointers, calling SetActiveChild() at every ancestor
    // along the way -- but at WindowManager-construction time,
    // rootComponent_ has no parent yet (main.cpp hasn't built bufferRow/
    // head around it yet), so that walk terminates immediately at
    // rootComponent_ itself instead of reaching all the way up through
    // bufferRow and head. Every *ancestor* container's own focus-selector
    // (bufferRow's, head's) is left at its untouched default (child 0) as
    // a result, meaning keyboard events sent to head never actually reach
    // any BufferView at all -- confirmed as the root cause of split/close/
    // other-window keybindings silently doing nothing in the real running
    // app despite passing every headless WindowManagerTest.cpp case (which
    // feeds events directly to RootComponent(), never embedding it in a
    // larger tree, so this exact ordering bug had no way to surface
    // there). Calling this again, once, after head is fully assembled --
    // the same call site the pre-window-splitting code's own
    // bufferView->TakeFocus() used to occupy -- fixes it: by then every
    // ancestor genuinely exists, and TakeFocus()'s walk reaches all the
    // way up.
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

  private:
    [[nodiscard]] std::unique_ptr<Pane> MakePane(text::Buffer& buffer, editor::Mode mode);

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

    // Rebuilds rootComponent_'s children from the current root_ tree shape
    // -- called after every structural mutation (split/close). Does NOT by
    // itself restore focus -- callers must explicitly TakeFocus()
    // afterward. FTXUI -> Notcurses migration: the "every freshly-built
    // intermediate Container's own focus-selector defaults to its first
    // child" reasoning this comment used to cite doesn't even apply
    // anymore -- Layout.h's Container has no focus-selector concept at all
    // (see Widget.h's own focus-registry comment), but a fresh TakeFocus()
    // is still required regardless, since RebuildComponentTree can
    // reparent/replace Containers without touching which Widget the global
    // focus registry (Widget.cpp) currently points at, which could easily
    // no longer be part of the tree at all after a DeleteWindow.
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

    text::KillRing&                killRing_;
    editor::RegisterTable&         registers_;
    text::BufferList&              bufferList_;
    const editor::CommandRegistry& registry_;
    const editor::Keymap&          janetKeymap_;
    const editor::Keymap&          globalKeymap_;
    std::string&                   statusMessage_;
    const Theme&                   theme_;
    ProjectSidebar*                projectSidebar_ = nullptr;
    editor::lsp::LspManager*       lspManager_     = nullptr;
    EventLoop*                     eventLoop_      = nullptr; // see SetEventLoop

    std::unique_ptr<WindowNode> root_;
    Container                   rootComponent_{Axis::Vertical, {}};

    // See StartAutoSaveTimer's own comment above.
    std::jthread autoSaveThread_;
};

} // namespace ned::ui

#endif // NED_UI_WINDOWMANAGER_H
