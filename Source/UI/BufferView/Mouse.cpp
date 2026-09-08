//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// Mouse event handling: click/drag selection, gutter clicks, and the context menu.
//

#include "UI/BufferView/Internal.h"

namespace ned::ui {

// The file-local helpers these definitions call live in BufferView/Internal.h
// now that several parts share them -- see that header. This using-directive is
// what let the split leave every call site untouched.
using namespace detail;

void BufferView::SetOnContextMenuChanged(std::function<void(std::optional<ListPopupModel>)> handler) {
    onContextMenuChanged_ = std::move(handler);
}

BufferView::ContextMenuEntry BufferView::ContextMenuCommandEntry(const std::string& commandName) const {
    return ContextMenuEntry{.label = StaticContextMenuLabel(commandName), .commandName = commandName};
}

void BufferView::ShowContextMenuAt(Point localClick) {
    text::Buffer&      buffer      = activeBuffer_.Get();
    const std::size_t  gutterWidth = GutterWidth();
    const bool         inGutter    = localClick.x <= static_cast<int>(gutterWidth); // mirrors
                                      // ByteOffsetForPoint's own "x > gutterWidth is content" boundary

    contextMenuEntries_.clear();
    bool        requestCodeActions = false;
    std::size_t codeActionPoint    = 0;

    if (inGutter) {
        // Gutter menu: line-resolve the same way the existing fold-gutter
        // click does (AdvanceVisibleLines(topLine_, y, totalLines)), not
        // ByteOffsetForPoint's fancier column-aware walk -- matching that
        // precedent exactly rather than introducing a second,
        // slightly-different line-resolution path for the gutter.
        const text::ITextStorage& content    = buffer.Content();
        const std::size_t         totalLines = content.LineCount();
        const std::size_t         line       = std::min(
            AdvanceVisibleLines(topLine_, static_cast<std::size_t>(std::max(localClick.y, 0)), totalLines),
            totalLines - 1);
        buffer.SetPoint(content.LineToByteOffset(line));

        // v1 simplification: gate each row by that command's own already-
        // active/inactive precondition, not by precisely which gutter
        // sub-column was clicked -- see ShowContextMenuAt's own doc
        // comment in BufferView.h.
        if (FoldGutterActive()) {
            contextMenuEntries_.push_back(ContextMenuCommandEntry("code-fold-toggle"));
        }
        if (DapGutterActive()) {
            contextMenuEntries_.push_back(ContextMenuCommandEntry("dap-toggle-breakpoint"));
        }
        if (BlameGutterActive()) {
            contextMenuEntries_.push_back(ContextMenuCommandEntry("vcs-blame-detail-at-point"));
        }
        // mouse-ergonomics follow-up: same v1 simplification as the three
        // gates above -- shown whenever the diff gutter is active for this
        // buffer at all, not gated on whether this exact line is staged vs
        // unstaged (vcs-stage-hunk/vcs-unstage-hunk each already report
        // their own "no {un,}staged change at this line" when it doesn't
        // apply). vcs-revert-hunk asks its own y/n first (ConfirmRevertHunk)
        // regardless of entry point, keyboard or here.
        if (DiffGutterActive()) {
            contextMenuEntries_.push_back(ContextMenuCommandEntry("vcs-stage-hunk"));
            contextMenuEntries_.push_back(ContextMenuCommandEntry("vcs-unstage-hunk"));
            contextMenuEntries_.push_back(ContextMenuCommandEntry("vcs-revert-hunk"));
        }
        // test-runner-gaps follow-up: the keyboard-equivalent escape hatch
        // for the left-click above, and the same v1 simplification as every
        // gate here -- shown whenever the test gutter is active at all, with
        // run-test-at-point reporting its own "no test definition at point"
        // when this line isn't one. The SetPoint above already moved point
        // to the clicked line, which is what that command resolves against.
        if (TestGutterActive()) {
            contextMenuEntries_.push_back(ContextMenuCommandEntry("run-test-at-point"));
        }
    }
    else {
        const std::size_t offset = ByteOffsetForPoint(localClick);
        // Deliberately does NOT ClearMark() the way a plain left click does
        // -- kill-region/kill-ring-save are documented no-ops without an
        // existing mark, and a right-click that silently discarded a
        // just-made selection before the user can even choose Cut/Copy
        // from it would be actively hostile.
        buffer.SetPoint(offset);

        // format-buffer/project-find-references are deliberately outside
        // this gate -- FormatCommand() is a separate, non-LSP configured
        // formatter, and project-find-references falls back to a
        // project-wide RE2 text scan when no server is running (see
        // ROADMAP's own note on that fallback path). Only the two commands
        // with no non-LSP fallback at all (goto-definition/rename --
        // exactly what RequestDefinitionAtPoint/RequestRenameAtPoint would
        // themselves hit a dead end against) are hidden without a real
        // connection for this buffer's own primary language --
        // lspManager_ merely being wired to this pane isn't that signal.
        // ActiveServerKeysForBuffer (ModeLine's own status-glyph source)
        // is the public surface for this; PrimarySyncState itself is
        // LspManager-private, so this checks for the buffer's own primary
        // language key among the *active* keys directly rather than
        // kProseLanguageKey (which never supports either) alone being
        // enough.
        bool hasLsp = false;
        if (lspManager_) {
            const std::string              primaryKey = editor::LanguageKeyForMode(mode_);
            const std::vector<std::string> active      = lspManager_->ActiveServerKeysForBuffer(buffer);
            hasLsp                                     = std::find(active.begin(), active.end(), primaryKey) != active.end();
        }
        contextMenuEntries_.push_back(ContextMenuCommandEntry("kill-region"));
        contextMenuEntries_.push_back(ContextMenuCommandEntry("kill-ring-save"));
        contextMenuEntries_.push_back(ContextMenuCommandEntry("yank"));
        if (hasLsp) {
            contextMenuEntries_.push_back(ContextMenuCommandEntry("lsp-goto-definition"));
        }
        contextMenuEntries_.push_back(ContextMenuCommandEntry("project-find-references"));
        if (hasLsp) {
            contextMenuEntries_.push_back(ContextMenuCommandEntry("lsp-rename"));
        }
        contextMenuEntries_.push_back(ContextMenuCommandEntry("format-buffer"));

        // context-aware-menu-round-2 follow-up: fired after the static rows
        // above are already committed (see below) rather than here, so a
        // fast/synchronous response's own inputMode_ guard already sees
        // ContextMenu set -- see RequestContextMenuCodeActions' own doc
        // comment.
        if (hasLsp) {
            requestCodeActions = true;
            codeActionPoint     = offset;
        }
    }

    if (contextMenuEntries_.empty()) {
        return; // defensive -- can't currently happen, see ShowContextMenuAt's own doc comment
    }

    contextMenuSelection_ = 0;
    const Box& box        = Box_();
    contextMenuAnchor_     = Point{.x = box.x_min + localClick.x, .y = box.y_min + localClick.y};
    inputMode_             = InputMode::ContextMenu;
    RefreshContextMenuStatus();

    if (requestCodeActions) {
        RequestContextMenuCodeActions(codeActionPoint);
    }
}

void BufferView::RequestContextMenuCodeActions(std::size_t point) {
    if (!lspManager_) {
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   generation = contextMenuCodeActionRequest_.Begin();

    // Same diagnostic-at-point range/server-routing preference as
    // RequestCodeActionsAtPoint -- see that method's own doc comment.
    std::size_t rangeStart = point;
    std::size_t rangeEnd   = point;
    std::string serverKey  = ResolvedLspServerKey(point);
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        const bool atPoint = (diagnostic.startByte == diagnostic.endByte) ? (point == diagnostic.startByte)
                                                                          : (diagnostic.startByte <= point && point < diagnostic.endByte);
        if (atPoint) {
            rangeStart = diagnostic.startByte;
            rangeEnd   = diagnostic.endByte;
            if (diagnostic.origin == text::Buffer::Diagnostic::Origin::Prose) {
                serverKey = editor::lsp::kProseLanguageKey;
            }
            break;
        }
    }

    lspManager_->RequestCodeActions(
        buffer, rangeStart, rangeEnd,
        [this, bufferPtr, point, generation, serverKey](std::vector<editor::lsp::CodeAction> actions) {
            if (contextMenuCodeActionRequest_.IsStale(generation)) {
                return; // superseded by a newer right-click
            }
            if (inputMode_ != InputMode::ContextMenu) {
                return; // menu cancelled/reopened elsewhere since
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent -- see RequestCompletionAtPoint's own identical guard
            }
            if (actions.empty()) {
                return; // nothing to splice in -- leave the static menu exactly as it already is
            }
            codeActionServerKey_ = serverKey; // read by ResolveAndApplyCodeAction when one of these is chosen
            std::vector<ContextMenuEntry> fixRows;
            fixRows.reserve(actions.size() + 1);
            for (editor::lsp::CodeAction& action : actions) {
                ContextMenuEntry entry;
                entry.label = action.title;
                entry.codeAction = std::move(action);
                fixRows.push_back(std::move(entry));
            }
            fixRows.push_back(ContextMenuEntry{.label = ContextMenuDividerRule(), .isDivider = true});

            contextMenuEntries_.insert(contextMenuEntries_.begin(), std::make_move_iterator(fixRows.begin()),
                                       std::make_move_iterator(fixRows.end()));
            contextMenuSelection_ = 0; // land on the first (highest-priority) live fix
            RefreshContextMenuStatus();
        },
        serverKey);
}

void BufferView::RefreshContextMenuStatus() {
    if (!onContextMenuChanged_) {
        return;
    }
    ListPopupModel model;
    model.title = "Menu";
    model.rows.reserve(contextMenuEntries_.size());
    std::size_t ordinal = 0;
    for (const ContextMenuEntry& entry : contextMenuEntries_) {
        if (entry.isDivider) {
            model.rows.push_back({.left = "", .main = entry.label});
            continue;
        }
        ++ordinal;
        model.rows.push_back({.left = std::to_string(ordinal) + ")", .main = entry.label});
    }
    model.selectedIndex = contextMenuSelection_;
    model.anchor         = contextMenuAnchor_;
    onContextMenuChanged_(std::move(model));
}

std::size_t BufferView::NextContextMenuIndex(std::size_t from, bool forward) const {
    const std::size_t count = contextMenuEntries_.size();
    std::size_t        index = from;
    do {
        index = forward ? (index + 1) % count : (index + count - 1) % count;
    } while (contextMenuEntries_[index].isDivider);
    return index;
}

std::optional<std::size_t> BufferView::ContextMenuIndexForDigit(std::size_t digit) const {
    std::size_t ordinal = 0;
    for (std::size_t i = 0; i < contextMenuEntries_.size(); ++i) {
        if (contextMenuEntries_[i].isDivider) {
            continue;
        }
        if (++ordinal == digit) {
            return i;
        }
    }
    return std::nullopt;
}

void BufferView::RunContextMenuEntry(std::size_t index) {
    if (index >= contextMenuEntries_.size() || contextMenuEntries_[index].isDivider) {
        return;
    }
    const ContextMenuEntry entry = contextMenuEntries_[index]; // copy -- EndInteractiveSession clears contextMenuEntries_ below
    EndInteractiveSession();
    if (entry.codeAction) {
        ResolveAndApplyCodeAction(*entry.codeAction);
        return;
    }
    editor::CommandContext context = MakeContext();
    context.viewportHeight         = size().height > 0 ? static_cast<std::size_t>(size().height) : 0;
    RunCommandAndHandleOutcome(context, [&] {
        dispatcher_.Registry().Invoke(entry.commandName, context);
        return true;
    });
}

void BufferView::HandleContextMenuKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_ = "Context menu cancelled.";
        EndInteractiveSession();
        return;
    }
    if (chord.Special == editor::SpecialKey::Down) {
        contextMenuSelection_ = NextContextMenuIndex(contextMenuSelection_, /*forward=*/true);
        RefreshContextMenuStatus();
        return;
    }
    if (chord.Special == editor::SpecialKey::Up) {
        contextMenuSelection_ = NextContextMenuIndex(contextMenuSelection_, /*forward=*/false);
        RefreshContextMenuStatus();
        return;
    }
    if (IsPlainCharacter(chord) && chord.Codepoint >= U'1' && chord.Codepoint <= U'9') {
        const std::size_t digit = static_cast<std::size_t>(chord.Codepoint - U'0');
        if (const std::optional<std::size_t> index = ContextMenuIndexForDigit(digit)) {
            contextMenuSelection_ = *index;
        }
        // falls through to the same Confirm transition Enter performs below
    }
    else if (chord.Special != editor::SpecialKey::Enter) {
        return; // anything else is ignored -- stay in the menu
    }

    RunContextMenuEntry(contextMenuSelection_);
}

void BufferView::ActivateContextMenuAt(std::size_t index) {
    if (inputMode_ != InputMode::ContextMenu || index >= contextMenuEntries_.size()) {
        return;
    }
    RunContextMenuEntry(index);
}

// call/type-hierarchy follow-up. See HierarchyDirection/HierarchySession's
// own doc comments in BufferView.h for the overall session shape.

bool BufferView::HandleTestGutterClick(Point at) {
    if (!TestGutterActive()) {
        return false;
    }
    const std::size_t testStart = TestGutterColumnStart();
    if (at.x < static_cast<int>(testStart) || static_cast<std::size_t>(at.x) >= testStart + kTestWidth) {
        return false;
    }

    // Line resolution matches the fold-gutter click's own
    // AdvanceVisibleLines walk exactly, rather than introducing a second
    // slightly-different gutter line-resolution path.
    const text::ITextStorage& content    = activeBuffer_.Get().Content();
    const std::size_t         totalLines = content.LineCount();
    const std::size_t         line =
        std::min(AdvanceVisibleLines(topLine_, static_cast<std::size_t>(std::max(at.y, 0)), totalLines), totalLines - 1);

    EnsureTestGutterCache();
    const auto it = std::lower_bound(testGutterEntries_.begin(), testGutterEntries_.end(), line,
                                     [](const TestGutterEntry& entry, std::size_t target) { return entry.line < target; });
    if (it == testGutterEntries_.end() || it->line != line) {
        return true; // inside the column, just not on a marked row -- swallow, don't place point
    }
    if (TestRunPreconditionsMet()) {
        RunSingleTest(it->name);
    }
    return true;
}

bool BufferView::OnMouseEvent(const Event& event) {
    const MouseEvent rawMouse = event.mouse();
    LogMouseEvent(MouseEventTag(rawMouse), rawMouse);

    // hover-tooltips follow-up: any mouse activity except the bare
    // no-button hover-move signal itself (see MaybeScheduleHover's own doc
    // comment for why that specific combination is button=None/
    // motion=Released rather than Motion::Moved) dismisses a pending/shown
    // tooltip outright -- a click, a drag, a wheel scroll, a real button
    // release. Checked once, up front, rather than at every one of the
    // click/drag branches below, so a new mouse-handling branch added later
    // can't accidentally forget it. A hover-move that lands outside this
    // widget's own box dismisses too -- there's no separate "mouse left me"
    // event in this codebase (every leaf just stops being handed events
    // whose position falls outside its Box_(), see Widget.h's own
    // LocalMouseEvent comment), so without this check, moving the mouse
    // straight off the buffer into the sidebar/tab bar/mode line/another
    // pane would leave a stale tooltip on screen forever -- confirmed live
    // (tmux smoke test) before this line was added.
    const bool isHoverMove = rawMouse.button == MouseEvent::Button::None && rawMouse.motion == MouseEvent::Motion::Released;
    if (!isHoverMove || !Box_().Contain(rawMouse.at.x, rawMouse.at.y)) {
        DismissHover();
    }

    // A growing sidebar-resize drag (round-2 sidebar follow-up; unified-
    // left-dock follow-up: the resize divider is LeftDock's now, not
    // ProjectSidebar's own) can deliver move/release events while the
    // cursor is over BufferView, not LeftDock itself -- checked first,
    // regardless of position (every leaf widget receives every mouse
    // event; see Widget.h's own header comment), taking priority over
    // BufferView's own handling.
    if (leftDock_ != nullptr && leftDock_->IsResizing()) {
        if (rawMouse.motion == MouseEvent::Motion::Moved) {
            leftDock_->UpdateResize(rawMouse.at.x);
            return true;
        }
        if (rawMouse.motion == MouseEvent::Motion::Released) {
            leftDock_->EndResize();
            return true;
        }
    }

    // project-sidebar-drag-drop follow-up: same "checked first, regardless
    // of position" cooperation as the resize guard just above -- a file
    // dragged out of ProjectSidebar and released over this pane opens it
    // here (not wherever happens to be focused), then tells ProjectSidebar
    // the drag is over (see ProjectSidebar::DraggingFilePath's own doc
    // comment for the full cross-widget contract, including why only the
    // one pane whose Box_() actually contains the drop performs the open --
    // every pane in a split layout receives this same event). A Moved event
    // is swallowed outright, regardless of position, so it can't also
    // start/extend this pane's own (unrelated) text selection while a file
    // is being dragged over it.
    if (projectSidebar_ != nullptr && projectSidebar_->DraggingFilePath()) {
        if (rawMouse.motion == MouseEvent::Motion::Moved) {
            return true;
        }
        // Real bug caught live (tmux smoke test, a two-pane split): calling
        // EndFileDrag() unconditionally here -- regardless of whether this
        // pane's own Box_() actually contained the drop -- meant whichever
        // pane's OnMouseEvent happened to run first for this Released event
        // (Container::OnEvent's fixed child order, not drop position) reset
        // dragPath_ before the pane the file was actually dropped on ever
        // got to check it, so the second pane's drop silently no-opped.
        // Gating the whole branch (not just the open) on Contain() first is
        // what makes only the one genuinely-targeted pane ever consume this.
        if (rawMouse.motion == MouseEvent::Motion::Released && Box_().Contain(rawMouse.at.x, rawMouse.at.y)) {
            const std::filesystem::path path = *projectSidebar_->DraggingFilePath();
            try {
                text::Buffer& opened = bufferList_.OpenOrCreateFile(path);
                activeBuffer_.Set(opened);
                statusMessage_ = "Opened " + opened.Name();
            }
            catch (const std::exception& e) {
                ReportError(e.what());
            }
            projectSidebar_->EndFileDrag();
            return true;
        }
    }

    // Split-resize follow-up: same "checked first, regardless of position"
    // cooperation as the ProjectSidebar guard just above -- a split divider
    // dragged past a neighboring pane's own edge delivers move/release
    // events here too. The divider itself (not this BufferView) owns the
    // drag; this only needs to stay out of the way so a stray Moved/Released
    // doesn't start or extend a stale text selection using this pane's own
    // (unrelated) dragAnchor_ while some other pane's divider is live.
    if (splitResizeQuery_ && splitResizeQuery_() &&
        (rawMouse.motion == MouseEvent::Motion::Moved || rawMouse.motion == MouseEvent::Motion::Released)) {
        return true;
    }

    const auto mouse = LocalMouseEvent(event);
    if (!mouse) {
        return false;
    }

    // Wheel scrolls the viewport without moving point, regardless of
    // InputMode -- unlike click/drag below, which only make sense in
    // Normal mode.
    if (mouse->button == MouseEvent::Button::WheelUp || mouse->button == MouseEvent::Button::WheelDown) {
        constexpr std::size_t kWheelScrollLines = 3;
        if (mouse->button == MouseEvent::Button::WheelUp) {
            SetTopLine((topLine_ > kWheelScrollLines) ? topLine_ - kWheelScrollLines : 0);
        }
        else {
            SetTopLine(topLine_ + kWheelScrollLines);
        }
        return true;
    }

    // horizontal-wheel-scroll follow-up: same unconditional-of-InputMode
    // shape as the vertical wheel case above. A no-op (but still consumed)
    // once EffectiveWrapLines() is true -- a wrapped line never extends past
    // the viewport width, matching ScrollToShowPointHorizontally's own
    // guard, so leftColumn_ has nothing left to scroll.
    if (mouse->button == MouseEvent::Button::WheelLeft || mouse->button == MouseEvent::Button::WheelRight) {
        if (!EffectiveWrapLines()) {
            constexpr std::size_t kWheelScrollColumns = 3;
            if (mouse->button == MouseEvent::Button::WheelLeft) {
                SetLeftColumn((leftColumn_ > kWheelScrollColumns) ? leftColumn_ - kWheelScrollColumns : 0);
            }
            else {
                SetLeftColumn(leftColumn_ + kWheelScrollColumns);
            }
        }
        return true;
    }

    // hover-tooltips follow-up: the bare hover-move signal itself (every
    // other mouse event already got DismissHover'd up front in OnMouseEvent).
    // Only meaningful in Normal mode -- an isearch/M-x/context-menu/etc.
    // session already dismissed any pending tooltip the moment it started
    // (OnKeyEvent's own DismissHover call), so there's nothing to schedule
    // here for those.
    if (mouse->button == MouseEvent::Button::None) {
        if (inputMode_ == InputMode::Normal) {
            MaybeScheduleHover(mouse->at);
        }
        return true;
    }

    // isearch-positional-click-exits follow-up: a left/middle-button press
    // during isearch isn't a search command either -- the same "not ours,
    // end the session and let it happen" treatment the motion-key catch-all
    // in HandleSearchKey just got. Checked here, before the
    // InputMode::Normal gates the middle-click-paste and plain-left-click
    // blocks below both have, so the very same click that ends the session
    // also places point (or pastes), in one motion, rather than requiring a
    // first click to dismiss and a second to actually act. Motion::Moved/
    // Released (a drag, or a button release with no matching press seen)
    // deliberately isn't included -- only a fresh press carries a clear
    // "the user meant to click here" position.
    if ((inputMode_ == InputMode::IsearchForward || inputMode_ == InputMode::IsearchBackward) &&
        mouse->motion == MouseEvent::Motion::Pressed &&
        (mouse->button == MouseEvent::Button::Left || mouse->button == MouseEvent::Button::Middle)) {
        if (!search_->Query().empty()) {
            lastSearchQuery_ = search_->Query();
        }
        search_->Accept();
        EndInteractiveSession();
    }

    // context-menu-mouse-dismiss follow-up: a left or middle press elsewhere
    // while the context menu is open ends the session first, then falls
    // through so the very same click also places point (or pastes) at the
    // new location -- same "click both ends the session and acts" shape as
    // the isearch positional-click block just above. (A click *on* the
    // popup itself never reaches here at all -- OverlayHost::OnMouseEvent
    // intercepts it before BufferView's own OnEvent runs.)
    if (inputMode_ == InputMode::ContextMenu && mouse->motion == MouseEvent::Motion::Pressed &&
        (mouse->button == MouseEvent::Button::Left || mouse->button == MouseEvent::Button::Middle)) {
        statusMessage_ = "Context menu cancelled.";
        EndInteractiveSession();
    }

    // right-click-context-menu follow-up: a right press in Normal mode opens
    // the menu at the click; a right press while the menu is already open
    // (inputMode_ == ContextMenu, possibly just re-entered Normal by the
    // dismiss block above) rebuilds it at the new click location instead,
    // the same "right-click elsewhere moves the menu" convention every
    // mainstream editor already has. Any other modal session (isearch,
    // M-x, ...) leaves right-click unhandled, same guard the middle-click
    // block below already has -- it isn't this feature's job to interpret a
    // stray right-click during an unrelated session.
    if (mouse->button == MouseEvent::Button::Right && mouse->motion == MouseEvent::Motion::Pressed) {
        if (inputMode_ != InputMode::Normal && inputMode_ != InputMode::ContextMenu) {
            return false;
        }
        TakeFocus();
        ShowContextMenuAt(mouse->at);
        return true;
    }

    // Middle-click-paste follow-up: X11/Wayland's own "click to insert the
    // primary selection" convention, independent of the kill ring/system
    // clipboard C-y uses. Point still moves to the click (matching plain
    // left-click's own unconditional SetPoint below, ReadOnly included) even
    // when there's nothing to paste (no primary-selection tool resolved, or
    // an empty selection) -- only the insert itself is skipped, and only for
    // a read-only buffer (InsertAtPoint throws there, with no command-
    // registry catch net to fall back on at this call site).
    if (mouse->button == MouseEvent::Button::Middle && mouse->motion == MouseEvent::Motion::Pressed) {
        if (inputMode_ != InputMode::Normal) {
            return false;
        }
        TakeFocus();
        text::Buffer& buffer = activeBuffer_.Get();
        buffer.ClearMark();
        buffer.SetPoint(ByteOffsetForPoint(mouse->at));
        if (!buffer.ReadOnly()) {
            if (const std::optional<std::string> pasted = editor::PasteFromPrimarySelection()) {
                buffer.InsertAtPoint(*pasted);
            }
        }
        return true;
    }

    if (inputMode_ != InputMode::Normal || mouse->button != MouseEvent::Button::Left) {
        return false;
    }

    if (mouse->motion == MouseEvent::Motion::Pressed) {
        // Window-splitting follow-up: harmless/no-op today (the sole
        // focusable widget already), necessary once multiple BufferViews
        // exist side by side -- a click into a pane is how focus moves
        // there, mirroring real Emacs' own "clicking a window selects it."
        TakeFocus();

        // main-editor-sticky-scroll follow-up: click-to-jump, checked first
        // (same "one specific region wins over the generic click fallthrough"
        // shape the fold-gutter click check just below already has) -- a
        // click inside the pinned band moves point to that ancestor's own
        // definition start instead of falling through to ByteOffsetForPoint,
        // which would otherwise resolve it against whatever real buffer line
        // the pinned rows are currently covering.
        if (stickyRowCount_ > 0 && mouse->at.y < stickyRowCount_) {
            const std::vector<editor::SymbolMarker> chain = StickyScrollChainForCurrentViewport();
            const auto                              index = static_cast<std::size_t>(mouse->at.y);
            if (index < chain.size()) {
                text::Buffer& buffer = activeBuffer_.Get();
                buffer.ClearMark();
                buffer.SetPoint(chain[index].startByte);
                ScrollToShowPoint();
            }
            return true;
        }

        // test-runner-gaps follow-up: a click on a test's own gutter mark
        // runs that one test, the mouse counterpart to run-test-at-point's
        // C-c T . -- same "one specific gutter region wins over the generic
        // point-placement fallthrough" shape the fold-gutter click below
        // has. Checked first only because the two regions can't overlap;
        // order between them carries no meaning.
        if (HandleTestGutterClick(mouse->at)) {
            return true;
        }

        // depth-aware-fold-gutter follow-up: a click inside the reserved
        // fold-depth region (see GutterWidth()/Paint()'s own doc comments)
        // toggles the fold at that row/column instead of placing point --
        // checked first, ahead of the generic point-placement path below,
        // the same "one specific gutter region wins over the generic click
        // fallthrough" shape a click inside the gutter more broadly already
        // has in ByteOffsetForPoint. The clicked column names which block
        // to toggle directly (a click at column 1 toggles only a depth-1
        // block, not whatever's innermost at that line) -- clicking a plain
        // guide line ('│'/'└', not a header cell) is a no-op, matching how
        // indent guides are inert-to-click in every mainstream editor.
        const std::size_t foldColumnWidth  = FoldGutterActive() ? kMaxFoldDepthColumns : 0;
        const std::size_t blameColumnWidth = BlameGutterActive() ? kBlameWidth : 0;
        // Mirrors GutterWidth()/Paint()'s own
        // [status][gap][digits][gap][symbol][fold][blame] layout -- foldStart
        // is where the fold region actually starts on screen: GutterWidth()
        // minus fold's and blame's own widths leaves everything to fold's
        // *left* (dap/diff/status/diagnostic/line-numbers/symbol, gutter-
        // symbol-kind follow-up), which is exactly foldStart -- no separate
        // symbol subtraction needed here, unlike the other three call sites
        // this check's own doc comment warns about, since this one derives
        // from the already-symbol-aware GutterWidth() instead of summing
        // column starts independently.
        const std::size_t foldStart = GutterWidth() - foldColumnWidth - blameColumnWidth;
        if (foldColumnWidth > 0 && mouse->at.x >= static_cast<int>(foldStart) &&
            static_cast<std::size_t>(mouse->at.x) < foldStart + foldColumnWidth) {
            text::Buffer&     buffer        = activeBuffer_.Get();
            const text::ITextStorage& content       = buffer.Content();
            const std::size_t totalLines    = content.LineCount();
            const std::size_t line          = std::min(AdvanceVisibleLines(topLine_, static_cast<std::size_t>(std::max(mouse->at.y, 0)), totalLines),
                                                       totalLines - 1);
            const int         clickedColumn = mouse->at.x - static_cast<int>(foldStart);
            EnsureFoldGutterCache();
            auto it = std::lower_bound(foldGutterEntries_.begin(), foldGutterEntries_.end(), line,
                                       [](const FoldGutterEntry& entry, std::size_t targetLine) { return entry.headerLine < targetLine; });
            for (; it != foldGutterEntries_.end() && it->headerLine == line; ++it) {
                if (it->column == clickedColumn) {
                    const bool collapsed = buffer.FoldMarkerAt(it->blockStart).has_value();
                    buffer.SetFoldMarker(it->blockStart,
                                         collapsed ? std::nullopt : std::optional(text::Buffer::FoldMarker::Collapsed));
                    break;
                }
            }
            return true;
        }

        text::Buffer&     buffer = activeBuffer_.Get();
        const std::size_t offset = ByteOffsetForPoint(mouse->at);
        buffer.ClearMark();
        buffer.SetPoint(offset);

        // Universal-clickable-affordances follow-up: Ctrl+Click opens the
        // link under the click, the mouse counterpart to open-link-at-
        // point's own C-c C-l -- same VS Code/browser convention (plain
        // click still just places point, matching every other mode) and
        // available in every mode without any per-mode wiring, since
        // OpenLinkAtPoint() already tries Org's bracket links first and
        // falls back to the generic bare-URL/file-path scan for everything
        // else. Takes priority over the read-only visit-result click below
        // -- an explicit Ctrl+Click is a more specific request than a plain
        // click, so it shouldn't silently fall back to a visit when the
        // clicked position isn't on a link.
        if (mouse->control) {
            clickCount_      = 0;
            lastClickOffset_ = std::nullopt;
            OpenLinkAtPoint();
            return true;
        }

        // Double/triple-click word/line selection -- same repeated-click-at-
        // the-same-spot detection ProjectSidebar's double-click-to-open uses
        // (kDoubleClickWindow), extended with a click count so a third click
        // selects the whole line instead of re-selecting the word. Skipped
        // on a read-only ("tossable") results buffer, where a click's job is
        // visiting the result under it, not selecting text.
        const auto now = std::chrono::steady_clock::now();
        clickCount_     = (lastClickOffset_.has_value() && *lastClickOffset_ == offset && (now - lastClickTime_) < kDoubleClickWindow)
                              ? (clickCount_ >= 3 ? 1 : clickCount_ + 1)
                              : 1;
        lastClickOffset_ = offset;
        lastClickTime_   = now;

        if (!buffer.ReadOnly() && clickCount_ >= 2) {
            const text::ITextStorage& content = buffer.Content();
            std::size_t       start;
            std::size_t       end;
            if (clickCount_ == 2) {
                const auto [wordStart, wordEnd] = WordBoundsAtOffset(content, offset);
                start                           = wordStart;
                end                             = wordEnd;
            }
            else {
                const std::size_t line = content.ByteOffsetToLine(offset);
                start                  = content.LineToByteOffset(line);
                end = line + 1 < content.LineCount() ? content.LineToByteOffset(line + 1) : content.ByteLength();
            }
            buffer.SetMark(start);
            buffer.SetPoint(end);
            dragAnchor_ = start;
            return true;
        }

        dragAnchor_ = offset;
        // project-search-visit-result follow-up: a click on a read-only
        // ("tossable") results buffer visits the result under the click,
        // the same "just press it" convention Enter now also follows
        // there -- see this method's own OnKeyEvent counterpart.
        if (buffer.ReadOnly()) {
            VisitSearchResult();
        }
        return true;
    }
    if (mouse->motion == MouseEvent::Motion::Moved) {
        text::Buffer& buffer = activeBuffer_.Get();
        if (!buffer.HasMark()) {
            buffer.SetMark(dragAnchor_);
        }
        buffer.SetPoint(ByteOffsetForPoint(mouse->at));
        ScrollToShowPoint();
        return true;
    }
    return false; // Released -- no behavior beyond the resize handoff above
}

void BufferView::LogMouseEvent(std::string_view event, const MouseEvent& mouse) const {
    if (!debugMouseLogPath_) {
        return;
    }

    std::ofstream log(*debugMouseLogPath_, std::ios::app);
    if (!log) {
        return;
    }

    const text::Buffer& buffer = activeBuffer_.Get();
    // mouse.x/y are absolute (screen-space) coordinates here -- coordinates
    // aren't translated before delivery (see Widget.h's own header comment),
    // and this logs the raw event as received, before LocalMouseEvent's own
    // translation.
    log << event << " at=(" << mouse.at.x << ',' << mouse.at.y << ')' << " button=" << static_cast<int>(mouse.button)
        << " inputMode=" << static_cast<int>(inputMode_) << " point=" << buffer.Point()
        << " mark=" << (buffer.HasMark() ? static_cast<long long>(buffer.Mark()) : -1LL) << " topLine=" << topLine_
        << " size=" << size().width << 'x' << size().height << '\n';
}

} // namespace ned::ui
