#include "WindowManager.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

#include <ftxui/dom/elements.hpp>

#include "Editor/ModeOverrides.h"
#include "Editor/ScratchPad.h"

namespace ned::ui {

namespace {

    // See WindowManager::StartAutoSaveTimer's own header comment for why
    // this moved here, verbatim, from BufferView.
    constexpr std::chrono::milliseconds kScratchAutoSaveInterval{5000};

    // A per-frame flex decorator, matching the exact lambda shape main.cpp's
    // own composition root already uses for the same purpose.
    ftxui::Element ApplyFlex(ftxui::Element element) {
        return ftxui::flex(std::move(element));
    }

} // namespace

Pane::Pane(text::Buffer& buffer, text::KillRing& killRing, editor::RegisterTable& registers,
           text::BufferList& bufferList, const editor::CommandRegistry& registry, const editor::Keymap& janetKeymap,
           const editor::Keymap& globalKeymap, editor::Mode mode, std::string& statusMessage, const Theme& theme,
           ProjectSidebar* projectSidebar, editor::lsp::LspManager* lspManager,
           std::function<void(editor::InteractiveRequest)> onWindowRequest,
           std::function<void(text::Buffer&)>              onBufferClosed) : activeBuffer_(buffer), mode_(std::move(mode)),
                                                                dispatcher_(registry, editor::KeymapStack({&janetKeymap, &mode_.keymap, &globalKeymap})),
                                                                bufferView_(std::make_shared<BufferView>(activeBuffer_, killRing, registers, bufferList, dispatcher_,
                                                                                                         statusMessage, mode_, theme)),
                                                                modeLine_(std::make_shared<ModeLine>(activeBuffer_, mode_, theme)),
                                                                scrollBar_(std::make_shared<ScrollBar>(theme.scrollBar)),
                                                                scrollUp_(std::make_shared<ScrollArrowButton>(U'▲', theme.scrollBar, theme.scrollBarDisabled)),
                                                                scrollDown_(std::make_shared<ScrollArrowButton>(U'▼', theme.scrollBar, theme.scrollBarDisabled)) {
    bufferView_->SetScrollBar(scrollBar_.get());
    bufferView_->SetScrollArrows(scrollUp_.get(), scrollDown_.get());
    bufferView_->SetProjectSidebar(projectSidebar);
    bufferView_->SetLspManager(lspManager);
    bufferView_->SetOnWindowRequest(std::move(onWindowRequest));
    bufferView_->SetOnBufferClosed(std::move(onBufferClosed));
    // per-buffer-mode follow-up: Mode is a property of the buffer being
    // viewed, not this pane -- reassigning mode_ in place is sufficient to
    // swap highlighting/folding/keymap/expand-selection all at once, since
    // dispatcher_'s KeymapStack above already points at &mode_.keymap (the
    // member's own stable address), not a snapshot taken at construction.
    bufferView_->SetOnActiveBufferChanged([this](text::Buffer& changedBuffer) { mode_ = editor::ModeForBuffer(changedBuffer); });

    scrollBar_->SetOnScroll(
        [this](int position) { bufferView_->SetTopLine(static_cast<std::size_t>(position)); });
    scrollUp_->SetOnClick([this] {
        const std::size_t top = bufferView_->TopLine();
        bufferView_->SetTopLine(top > 0 ? top - 1 : 0);
    });
    scrollDown_->SetOnClick([this] { bufferView_->SetTopLine(bufferView_->TopLine() + 1); });

    using namespace ftxui; // NOLINT -- Component (the type) is shadowed by Pane::Component() below; every
                           // local variable of that type is spelled out as ftxui::Component explicitly instead.

    ftxui::Component scrollColumn = Container::Vertical({
                                        scrollUp_ | size(HEIGHT, EQUAL, 1),
                                        scrollBar_ | ApplyFlex,
                                        scrollDown_ | size(HEIGHT, EQUAL, 1),
                                    }) |
                                    size(WIDTH, EQUAL, 1);

    ftxui::Component row = Container::Horizontal({
        bufferView_ | ApplyFlex,
        scrollColumn,
    });

    component_ = Container::Vertical({
        row | ApplyFlex,
        modeLine_ | size(HEIGHT, EQUAL, 1),
    });
}

ActiveBuffer& Pane::ActiveBufferRef() {
    return activeBuffer_;
}

BufferView& Pane::Buffer() {
    return *bufferView_;
}

const editor::Mode& Pane::ModeRef() const {
    return mode_;
}

ftxui::Component Pane::Component() const {
    return component_;
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
                             text::BufferList& bufferList, const editor::CommandRegistry& registry,
                             const editor::Keymap& janetKeymap, const editor::Keymap& globalKeymap,
                             editor::Mode initialMode, std::string& statusMessage,
                             const Theme& theme) : killRing_(killRing), registers_(registers), bufferList_(bufferList), registry_(registry), janetKeymap_(janetKeymap),
                                                   globalKeymap_(globalKeymap), statusMessage_(statusMessage), theme_(theme) {
    root_       = std::make_unique<WindowNode>();
    root_->kind = WindowNode::Kind::Leaf;
    root_->pane = MakePane(initialBuffer, std::move(initialMode));

    rootComponent_ = ftxui::Container::Vertical({});
    RebuildComponentTree();

    // Deliberately NOT calling root_->pane->Buffer().TakeFocus() here --
    // see this class's own public TakeFocus()'s doc comment (WindowManager.h)
    // for why that would be a real, confirmed-by-testing no-op at this
    // point (rootComponent_ has no parent yet) and main.cpp must call the
    // public version again once RootComponent() is actually embedded into
    // the app's full composition tree.
}

std::unique_ptr<Pane> WindowManager::MakePane(text::Buffer& buffer, editor::Mode mode) {
    return std::make_unique<Pane>(
        buffer, killRing_, registers_, bufferList_, registry_, janetKeymap_, globalKeymap_, std::move(mode),
        statusMessage_, theme_, projectSidebar_, lspManager_,
        [this](editor::InteractiveRequest request) { HandleWindowRequest(request); },
        [this](text::Buffer& closedBuffer) { HandleBufferClosed(closedBuffer); });
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
    }
}

ftxui::Component WindowManager::RootComponent() const {
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

void WindowManager::StartAutoSaveTimer(ftxui::ScreenInteractive& screen) {
    autoSaveThread_ = std::jthread([this, &screen](std::stop_token stopToken) {
        std::mutex                  mutex;
        std::condition_variable_any cv;
        while (!stopToken.stop_requested()) {
            std::unique_lock lock(mutex);
            if (cv.wait_for(lock, stopToken, kScratchAutoSaveInterval, [&stopToken] { return stopToken.stop_requested(); })) {
                return;
            }
            screen.Post([this] { editor::AutoSaveScratchBuffers(bufferList_); });
        }
    });
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
    // Computed once, shared across every affected pane -- not recomputed
    // (and not re-created, in the CreateBuffer("scratch") fallback case)
    // per pane, so N panes all showing the one buffer being closed end up
    // sharing a single fresh scratch buffer rather than each conjuring
    // their own.
    text::Buffer* replacement = nullptr;
    for (const auto& candidate : bufferList_.Buffers()) {
        if (candidate.get() != &closingBuffer) {
            replacement = candidate.get();
            break;
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
    rootComponent_->DetachAllChildren();
    // ApplyFlex here, not inside BuildComponent's own Leaf case, is what
    // makes a single, unsplit pane actually stretch to fill the available
    // height rather than taking its own natural minimum size -- a real bug
    // caught by manual pty testing (the buffer area rendered squished to
    // just its content's own line count, with the mode line immediately
    // below it instead of at the bottom of the screen): rootComponent_ is a
    // Vertical container, and an unflexed child of a Vertical container
    // gets exactly its own requested minimum height, not a share of
    // whatever's available, the same reasoning main.cpp's own composition
    // root already documents for why bufferRow's own children each need an
    // explicit size()/flex() decorator at their embedding point. The
    // SplitBelow/SplitRight cases in BuildComponent already apply this to
    // their own first/second children directly; this is the equivalent for
    // the one remaining case -- the whole tree embedded, once, into
    // rootComponent_ itself.
    rootComponent_->Add(BuildComponent(root_.get()) | ApplyFlex);
}

ftxui::Component WindowManager::BuildComponent(const WindowNode* node) const {
    using namespace ftxui;

    if (node->kind == WindowNode::Kind::Leaf) {
        return node->pane->Component();
    }

    Component first  = BuildComponent(node->first.get());
    Component second = BuildComponent(node->second.get());

    if (node->kind == WindowNode::Kind::SplitRight) {
        return Container::Horizontal({
            first | ApplyFlex,
            Renderer([] { return separator(); }),
            second | ApplyFlex,
        });
    }

    // SplitBelow -- the top pane's own ModeLine row already provides the
    // visual boundary, no separate divider needed (see this file's own
    // header comment).
    return Container::Vertical({
        first | ApplyFlex,
        second | ApplyFlex,
    });
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

} // namespace ned::ui
