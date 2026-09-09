//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// The interactive-session state machine: StartInteractiveSession/EndInteractiveSession,
// and every Handle*Key prompt handler with its Refresh*Status partner.
//

#include "UI/BufferView/Internal.h"

namespace ned::ui {

// The file-local helpers these definitions call live in BufferView/Internal.h
// now that several parts share them -- see that header. This using-directive is
// what let the split leave every call site untouched.
using namespace detail;

void BufferView::SetOnAcpPanelToggle(std::function<void()> handler) {
    onAcpPanelToggle_ = std::move(handler);
}

void BufferView::SetOnAcpRewindRequest(std::function<void()> handler) {
    onAcpRewindRequest_ = std::move(handler);
}

void BufferView::SetOnCandidatesChanged(std::function<void(std::optional<ListPopupModel>)> handler) {
    onCandidatesChanged_ = std::move(handler);
}

bool BufferView::HandleConflictQuickKey(const editor::KeyChord& chord) {
    if (!chord.Meta || chord.Control) {
        return false;
    }
    const char* commandName = nullptr;
    switch (chord.Codepoint) {
        case U'n':
            commandName = "next-conflict-hunk";
            break;
        case U'p':
            commandName = "previous-conflict-hunk";
            break;
        case U'o':
            commandName = "merge-take-ours";
            break;
        case U't':
            commandName = "merge-take-theirs";
            break;
        case U'b':
            commandName = "merge-take-both";
            break;
        case U'd':
            commandName = "merge-take-neither";
            break;
        case U'k':
            commandName = "merge-keep-base";
            break;
        default:
            return false;
    }
    const std::vector<text::ConflictHunk>& conflictHunks = gutters_.ConflictHunks();
    if (conflictHunks.empty()) {
        return false; // nothing unresolved -- M-o/t/b/d/k/n/p mean whatever they ordinarily do
    }
    editor::CommandContext context = MakeContext();
    RunCommandAndHandleOutcome(context, [&] {
        dispatcher_.Registry().Invoke(commandName, context);
        return true;
    });
    return true;
}

bool BufferView::HandleVimKey(const editor::KeyChord& chord) {
    if (vimEngine_.CurrentMode() == editor::vim::Mode::Insert) {
        if (IsQuit(chord)) {
            vimEngine_.ExitInsertToNormal(activeBuffer_.Get());
        }
        else {
            vimEngine_.RecordInsertKey(chord);
            // Vim's own Insert-mode Ctrl-chords (C-w/C-u/C-t/C-d/C-r) get first look --
            // otherwise they'd fall through to ned's ordinary Emacs bindings for the same
            // chords (kill-region, universal-argument, isearch-backward, ...), which is
            // not what a vim user typing C-w expects. See Engine::HandleInsertModeChord's
            // own doc comment for why this isn't just another KeymapStack layer.
            if (!vimEngine_.HandleInsertModeChord(activeBuffer_.Get(), chord)) {
                DispatchChordNormally(chord);
            }
        }
    }
    else {
        vimEngine_.SetViewport(viewport_.TopLine(), size().height > 0 ? static_cast<std::size_t>(size().height) : 0);
        vimEngine_.HandleKey(activeBuffer_.Get(), chord);
    }

    const editor::vim::PendingIntent intent = vimEngine_.TakePendingIntent();
    if (intent == editor::vim::PendingIntent::Quit) {
        if (eventLoop_) {
            eventLoop_->Exit();
        }
        return true;
    }
    if (intent == editor::vim::PendingIntent::CloseBuffer) {
        RequestCloseBuffer(activeBuffer_.Get()); // may destroy *this* -- nothing after
        return true;
    }

    // vim-global-marks follow-up: an uppercase-mark jump into a different file than the
    // one currently open -- Engine can't switch buffers itself (deliberately
    // UI-free), so it hands the target back here. Same open-then-jump shape
    // HandleBookmarkJumpKey already uses for its own (path, line, column) targets.
    if (const auto jump = vimEngine_.TakePendingBufferJump()) {
        try {
            text::Buffer& opened = bufferList_.OpenOrCreateFile(jump->path);
            activeBuffer_.Set(opened);
            opened.SetPoint(opened.ByteOffsetForLineAndColumn(jump->line, jump->column, static_cast<std::size_t>(editor::TabWidth())));
        }
        catch (const std::exception& e) {
            ReportError(e.what());
        }
    }

    if (!vimEngine_.StatusText().empty()) {
        statusMessage_ = vimEngine_.StatusText();
    }
    else if (vimEngine_.CurrentMode() != editor::vim::Mode::Normal) {
        statusMessage_ = "-- " + vimEngine_.ModeIndicator() + " --";
    }
    else {
        statusMessage_.clear();
    }
    ClampPointToNarrowing();
    // Applied before viewport_.ScrollToShowPoint() -- zz/zt/zb/C-e/C-y request an explicit viewport_.TopLine()
    // independent of point, and viewport_.ScrollToShowPoint() only nudges viewport_.TopLine() far enough to
    // keep point visible, so it leaves an already-visible point's explicit recenter alone.
    if (const auto pendingTop = vimEngine_.TakePendingTopLine()) {
        viewport_.SetTopLine(*pendingTop);
    }
    viewport_.ScrollToShowPoint();
    return true;
}

void BufferView::HandlePrefixArgumentKey(const editor::KeyChord& chord) {
    const editor::PrefixArgumentReader::Outcome outcome = prefixArgReader_->HandleKey(chord);
    if (outcome == editor::PrefixArgumentReader::Outcome::Continue) {
        statusMessage_ = prefixArgReader_->StatusText();
        return;
    }
    // Terminate: chord doesn't belong to prefix-argument syntax. Capture the
    // resolved value for the next dispatch, leave PrefixArgument mode, and
    // re-run this same chord through the identical path any other
    // Normal-mode keystroke goes through -- MakeContext() (called inside
    // DispatchChordNormally) picks pendingPrefixArg_ up from there.
    //
    // Keyboard-macro recording note: like isearch/query-replace, keystrokes
    // consumed here (the C-u itself excepted -- it's a normal single-chord
    // binding, so it does go through Dispatcher::Feed and gets recorded)
    // aren't individually recorded into an in-progress macro; only this
    // final re-dispatched chord is. Same pre-existing limitation every other
    // multi-keystroke InputMode session already has.
    pendingPrefixArg_ = prefixArgReader_->Value();
    prefixArgReader_.reset();
    inputMode_ = InputMode::Normal;
    DispatchChordNormally(chord);
}

bool BufferView::HandleSnippetNavigationKey(const editor::KeyChord& chord) {
    text::Buffer* buffer = ResolveSnippetBuffer();
    if (buffer == nullptr) {
        // Session buffer closed out from under the session (or no session at
        // all -- shouldn't happen, but never leave the mode wedged).
        EndSnippetSession();
        return true;
    }

    const bool plainTab = chord.Special == editor::SpecialKey::Tab && !chord.Control && !chord.Meta;
    if ((plainTab && !chord.Shift) || (chord.Meta && !chord.Control && chord.Codepoint == U'n')) {
        dispatcher_.RecordChord(chord);
        if (snippetSession_->NextField(*buffer) == editor::SnippetSession::NavResult::Finished) {
            EndSnippetSession();
            statusMessage_.clear();
        }
        else {
            statusMessage_ = snippetSession_->StatusText();
        }
        ClampPointToNarrowing();
        viewport_.ScrollToShowPoint();
        return true;
    }
    // S-TAB's arrival as a shifted Tab chord is terminal-dependent (see
    // KeyTranslation.cpp's special-key shift handling) -- M-p is the
    // always-available fallback, M-n its forward twin above.
    if ((plainTab && chord.Shift) || (chord.Meta && !chord.Control && chord.Codepoint == U'p')) {
        dispatcher_.RecordChord(chord);
        snippetSession_->PreviousField(*buffer);
        statusMessage_ = snippetSession_->StatusText();
        ClampPointToNarrowing();
        viewport_.ScrollToShowPoint();
        return true;
    }
    if (IsQuit(chord)) {
        // Done -- the expanded text stays exactly as it is.
        dispatcher_.RecordChord(chord);
        EndSnippetSession();
        statusMessage_.clear();
        return true;
    }
    if (chord.Special == editor::SpecialKey::Backspace && !chord.Control && !chord.Meta &&
        snippetSession_->Pristine()) {
        // Backspace on a pristine placeholder deletes the whole placeholder
        // and is consumed -- one undo step including the mirror sync.
        dispatcher_.RecordChord(chord);
        buffer->BeginUndoGroup();
        snippetSession_->DeleteActiveFieldContent(*buffer);
        snippetSession_->ClearPristine();
        snippetSession_->SyncMirrors(*buffer);
        buffer->EndUndoGroup();
        ClampPointToNarrowing();
        viewport_.ScrollToShowPoint();
        return true;
    }
    if (snippetSession_->Pristine()) {
        if (IsPlainCharacter(chord)) {
            // First typed character replaces the placeholder: arm the
            // delete for RunCommandAndHandleOutcome's pre-dispatch hook
            // (inside the same undo group as the keystroke itself).
            snippetPendingPristineDelete_ = true;
        }
        else {
            snippetSession_->ClearPristine();
        }
    }
    return false;
}

void BufferView::HandleSnippetKey(const editor::KeyChord& chord) {
    if (HandleSnippetNavigationKey(chord)) {
        return;
    }
    DispatchChordNormally(chord);
}

void BufferView::TriggerSwitchProject() {
    StartInteractiveSession(editor::InteractiveRequest::SwitchProject);
}

void BufferView::StartInteractiveSession(editor::InteractiveRequest request) {
    switch (request) {
        case editor::InteractiveRequest::IsearchForward:
            inputMode_ = InputMode::IsearchForward;
            search_.emplace(activeBuffer_.Get(), editor::IncrementalSearch::Direction::Forward);
            break;
        case editor::InteractiveRequest::IsearchBackward:
            inputMode_ = InputMode::IsearchBackward;
            search_.emplace(activeBuffer_.Get(), editor::IncrementalSearch::Direction::Backward);
            break;
        case editor::InteractiveRequest::QueryReplace:
            inputMode_ = InputMode::QueryReplace;
            queryReplace_.emplace(activeBuffer_.Get());
            break;
        case editor::InteractiveRequest::UniversalArgument:
            inputMode_ = InputMode::PrefixArgument;
            prefixArgReader_.emplace();
            statusMessage_ = prefixArgReader_->StatusText();
            return;
        case editor::InteractiveRequest::SnippetExpand:
            if (pendingSnippetExpansion_) {
                const auto request = std::move(*pendingSnippetExpansion_);
                pendingSnippetExpansion_.reset();
                BeginSnippetExpansion(request.replaceStart, request.replaceEnd, request.body);
            }
            return;
        case editor::InteractiveRequest::ConfirmQuit: {
            inputMode_ = InputMode::ConfirmQuit;
            std::string names;
            for (const auto& buffer : bufferList_.Buffers()) {
                if (buffer->Modified() && !buffer->ReadOnly()) {
                    if (!names.empty()) {
                        names += ", ";
                    }
                    names += buffer->Name();
                }
            }
            statusMessage_ = "Unsaved changes in: " + names + " -- quit anyway? (y/n)";
            return;
        }
        case editor::InteractiveRequest::FindFile:
            inputMode_ = InputMode::FindFile;
            prompt_.emplace("Find file: ");
            pathCompletionSelection_ = 0;
            RefreshPathCompletionPopup();
            return;
        case editor::InteractiveRequest::SwitchToBuffer:
            inputMode_ = InputMode::SwitchToBuffer;
            prompt_.emplace("Switch to buffer: ");
            switchToBufferList_.SelectTop();
            RefreshSwitchToBufferStatus();
            return;
        case editor::InteractiveRequest::ProjectSearch:
            inputMode_ = InputMode::ProjectSearch;
            prompt_.emplace("Project search: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::VisitSearchResult:
            VisitSearchResult();
            return;
        case editor::InteractiveRequest::ProjectReplace:
            inputMode_ = InputMode::ProjectReplace;
            projectReplace_.emplace(editor::ProjectRoot());
            statusMessage_ = projectReplace_->StatusText();
            return;
        case editor::InteractiveRequest::ToggleProjectSidebar:
            // unified-left-dock follow-up (migration step 3): ProjectSidebar
            // and VcsPanel are both hosted panels of the same LeftDock now,
            // which enforces their mutual exclusivity structurally (only
            // the dock's own active panel ever paints or receives events at
            // all) -- so this is just the rail-click gesture driven
            // programmatically: re-toggling the already-active, already-
            // expanded panel collapses; anything else expands+switches to
            // it. See LeftDock::ActivateOrToggle's own doc comment.
            if (leftDock_ != nullptr && projectSidebar_ != nullptr) {
                leftDock_->ActivateOrToggle(projectSidebar_);
            }
            return;
        case editor::InteractiveRequest::ToggleVcsPanel:
            // Same shape as ToggleProjectSidebar above, mirrored.
            if (leftDock_ != nullptr && vcsPanel_ != nullptr) {
                leftDock_->ActivateOrToggle(vcsPanel_);
            }
            return;
        case editor::InteractiveRequest::ToggleTerminal:
            // terminal-panel follow-up: one-shot direct action, same shape
            // as the window-management requests just below -- the panel
            // lives above this class, so only forward.
            if (onTerminalToggle_) {
                onTerminalToggle_();
            }
            return;
        case editor::InteractiveRequest::ToggleJanetRepl:
            // REPL-engine follow-up: ToggleTerminal/DapToggleConsole's own
            // shape -- the Janet REPL panel lives above this class, only
            // forward.
            if (onJanetReplToggle_) {
                onJanetReplToggle_();
            }
            return;
        case editor::InteractiveRequest::NewTerminal:
            // multiple-terminal-tabs follow-up: ToggleTerminal's own
            // one-shot-forward shape -- the terminal tabs live above this
            // class, only forward.
            if (onNewTerminalRequest_) {
                onNewTerminalRequest_();
            }
            return;
        case editor::InteractiveRequest::ListBuffers:
            // generic-popup follow-up: one-shot direct action, same shape as
            // ToggleTerminal above -- the buffer-list panel lives above this
            // class, so only forward.
            if (onBufferListToggle_) {
                onBufferListToggle_();
            }
            return;
        case editor::InteractiveRequest::FocusProjectSidebar:
            // sidebar-keyboard-focus follow-up, unified-left-dock follow-up
            // (migration step 3): LeftDock::PrepareForKeyboardFocus switches
            // to this panel if it isn't already active and expands the dock
            // if collapsed (focus into an invisible/inactive tree would be
            // meaningless), remembering to re-collapse on return -- see that
            // method's own doc comment; ProjectSidebar's own OnEvent still
            // drives the selection until it returns focus.
            if (leftDock_ != nullptr && projectSidebar_ != nullptr) {
                leftDock_->PrepareForKeyboardFocus(projectSidebar_);
                projectSidebar_->TakeFocus();
            }
            return;
        case editor::InteractiveRequest::FocusVcsPanel:
            // Same shape as FocusProjectSidebar above, mirrored.
            if (leftDock_ != nullptr && vcsPanel_ != nullptr) {
                leftDock_->PrepareForKeyboardFocus(vcsPanel_);
                vcsPanel_->TakeFocus();
            }
            return;
        case editor::InteractiveRequest::ToggleMinimap:
            // Same one-shot direct action shape as ToggleProjectSidebar
            // above, but flips both halves of the lockstep pair (see
            // BufferView.h's own comment on SetMinimap) -- exactly one of
            // the minimap/scroll-bar column ever occupies that screen
            // real estate.
            if (minimap_ != nullptr) {
                minimap_->active = !minimap_->active;
                if (!minimap_->active) {
                    // Paint() never runs again once active is false (Layout.h's
                    // Container skips inactive widgets outright), so this is the
                    // only place that can tear down a live pixel-blitter plane
                    // (Minimap.h's own ReleasePlane() doc comment).
                    minimap_->ReleasePlane();
                }
                editor::SetVariable("minimap-enabled", minimap_->active ? "true" : "false");
            }
            if (minimapScrollColumn_ != nullptr) {
                minimapScrollColumn_->active = !minimapScrollColumn_->active;
            }
            return;
        case editor::InteractiveRequest::ProjectAgenda:
            BuildAgendaMultibuffer();
            return;
        case editor::InteractiveRequest::ShowMessages:
            ShowMessagesBuffer();
            return;
        case editor::InteractiveRequest::OrgClockReport:
            BuildClockReportMultibuffer();
            return;
        case editor::InteractiveRequest::LspGotoDefinition:
            RequestDefinitionAtPoint(LspLocationKind::Definition);
            return;
        case editor::InteractiveRequest::LspGotoDeclaration:
            RequestDefinitionAtPoint(LspLocationKind::Declaration);
            return;
        case editor::InteractiveRequest::LspGotoTypeDefinition:
            RequestDefinitionAtPoint(LspLocationKind::TypeDefinition);
            return;
        case editor::InteractiveRequest::LspGotoImplementation:
            RequestDefinitionAtPoint(LspLocationKind::Implementation);
            return;
        case editor::InteractiveRequest::LspPeekDefinition:
            RequestPeekDefinitionAtPoint();
            return;
        case editor::InteractiveRequest::LspGotoSymbol:
            RequestDocumentSymbolsAtPoint();
            return;
        // symbol-search follow-up: unlike LspGotoSymbol, this session opens
        // right away (workspace/symbol has no local candidate list to wait
        // on, only a live server round trip) -- mirrors ExecuteCommand's own
        // "populate right away" shape, just via an async request instead of
        // a free in-memory lookup.
        case editor::InteractiveRequest::LspWorkspaceSymbol:
            if (!lspManager_) {
                statusMessage_ = "No LSP manager available.";
                return;
            }
            pendingWorkspaceSymbols_.clear();
            workspaceSymbolLabels_.clear();
            inputMode_ = InputMode::LspWorkspaceSymbol;
            prompt_.emplace("Workspace symbol: ");
            workspaceSymbolSelection_ = 0;
            RequestWorkspaceSymbolsForCurrentQuery();
            return;
        // call/type-hierarchy follow-up: four more one-shot direct actions,
        // same shape as LspGotoSymbol above -- RequestHierarchyAtPoint owns
        // the actual prepare/expand/browse session.
        case editor::InteractiveRequest::LspCallHierarchyIncoming:
            RequestHierarchyAtPoint(HierarchyDirection::IncomingCalls);
            return;
        case editor::InteractiveRequest::LspCallHierarchyOutgoing:
            RequestHierarchyAtPoint(HierarchyDirection::OutgoingCalls);
            return;
        case editor::InteractiveRequest::LspTypeHierarchySupertypes:
            RequestHierarchyAtPoint(HierarchyDirection::Supertypes);
            return;
        case editor::InteractiveRequest::LspTypeHierarchySubtypes:
            RequestHierarchyAtPoint(HierarchyDirection::Subtypes);
            return;
        case editor::InteractiveRequest::SwitchHeaderSource:
            SwitchHeaderSource();
            return;
        case editor::InteractiveRequest::JumpBack:
            JumpBack();
            return;
        case editor::InteractiveRequest::JumpForward:
            JumpForward();
            return;
        case editor::InteractiveRequest::LspRename:
            RequestPrepareRenameAtPoint();
            return;
        case editor::InteractiveRequest::LspLinkedEditingRange:
            RequestLinkedEditingRangeAtPoint();
            return;
        case editor::InteractiveRequest::LspShowLog: {
            const std::string logName = std::string(editor::lsp::kLspLogBufferName);
            text::Buffer*     log     = bufferList_.Find(logName);
            if (!log) {
                log = &bufferList_.CreateBuffer(logName);
                log->SetReadOnly(true);
            }
            activeBuffer_.Set(*log);
            return;
        }
        case editor::InteractiveRequest::KillBuffer:
            RequestCloseBuffer(activeBuffer_.Get());
            return;
        case editor::InteractiveRequest::TabNext:
        case editor::InteractiveRequest::TabPrevious: {
            // Tab-cycling follow-up: one-shot direct action -- next/previous
            // in Buffers() (tab bar) order, wrapping at either end. Set()
            // fires the MRU touch hook like any other switch, so tab-cycling
            // and MRU close stay consistent for free.
            const auto&         buffers = bufferList_.Buffers();
            const text::Buffer* active  = &activeBuffer_.Get();
            for (std::size_t i = 0; i < buffers.size(); ++i) {
                if (buffers[i].get() != active) {
                    continue;
                }
                const std::size_t count = buffers.size();
                const std::size_t next  = (request == editor::InteractiveRequest::TabNext)
                                              ? (i + 1) % count
                                              : (i + count - 1) % count;
                activeBuffer_.Set(*buffers[next]);
                break;
            }
            return;
        }
        case editor::InteractiveRequest::Recenter: {
            // One-shot direct action, same shape as ToggleProjectSidebar --
            // viewport_.TopLine() is this widget's own state, so the command can only
            // request the scroll. SetTopLine clamps via MaxTopLine.
            text::Buffer&     buffer    = activeBuffer_.Get();
            const std::size_t pointLine = buffer.Content().ByteOffsetToLine(buffer.Point());
            const std::size_t half      = static_cast<std::size_t>(std::max(0, size().height)) / 2;
            viewport_.SetTopLine(pointLine > half ? pointLine - half : 0);
            return;
        }
        case editor::InteractiveRequest::GotoLine:
            inputMode_ = InputMode::GotoLine;
            prompt_.emplace("Goto line: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::ConfirmOverwriteSave:
            inputMode_     = InputMode::ConfirmOverwriteSave;
            statusMessage_ = activeBuffer_.Get().Name() + " changed on disk since it was read; save anyway? (y/n)";
            return;
        case editor::InteractiveRequest::ConfirmSaveWithConflicts:
            inputMode_     = InputMode::ConfirmSaveWithConflicts;
            statusMessage_ = activeBuffer_.Get().Name() + " still has unresolved <<<<<<< conflict markers; save anyway? (y/n)";
            return;
        case editor::InteractiveRequest::ConfirmRevertHunk:
            inputMode_     = InputMode::ConfirmRevertHunk;
            statusMessage_ = "Discard this hunk's uncommitted change? This cannot be undone. (y/n)";
            return;
        case editor::InteractiveRequest::CreateDirectory:
            inputMode_ = InputMode::CreateDirectory;
            prompt_.emplace("Create directory: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::DeleteFile:
            inputMode_   = InputMode::DeleteFile;
            deleteStage_ = DeleteFileStage::EnteringPath;
            prompt_.emplace("Delete file: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::RenameFile:
            inputMode_   = InputMode::RenameFile;
            renameStage_ = RenameFileStage::EnteringSource;
            prompt_.emplace("Rename file: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::FindScratch:
            inputMode_ = InputMode::FindScratch;
            prompt_.emplace("Find scratch: ");
            pathCompletionSelection_ = 0;
            RefreshPathCompletionPopup();
            return;
        case editor::InteractiveRequest::RecoverFile: {
            // backup-and-recovery follow-up: both no-session outcomes
            // (pathless buffer, nothing backed up) report and stay Normal.
            text::Buffer& buffer = activeBuffer_.Get();
            if (!buffer.Path().has_value()) {
                statusMessage_ = "Buffer " + buffer.Name() + " has no file to recover";
                return;
            }
            recoverVersions_ = editor::ListBackupVersions(*buffer.Path());
            if (recoverVersions_.empty()) {
                statusMessage_ = "No backups for " + buffer.Name();
                return;
            }
            inputMode_    = InputMode::RecoverFile;
            recoverStage_ = RecoverFileStage::PickingVersion;
            prompt_.emplace("Recover " + buffer.Name() + " -- version (1-" + std::to_string(recoverVersions_.size()) + ", Enter=1): ");
            std::string candidates;
            for (std::size_t index = 0; index < recoverVersions_.size(); ++index) {
                candidates += (index == 0 ? "" : ", ") + std::to_string(index + 1) + ": " + recoverVersions_[index].label;
            }
            statusMessage_ = prompt_->StatusText() + "  {" + candidates + "}";
            return;
        }
        case editor::InteractiveRequest::RunTask:
            taskPromptAction_ = TaskPromptAction::Run;
            inputMode_        = InputMode::TaskName;
            prompt_.emplace("Run task: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::CancelTask:
            taskPromptAction_ = TaskPromptAction::Cancel;
            inputMode_        = InputMode::TaskName;
            prompt_.emplace("Cancel task: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::RunRepl:
            inputMode_ = InputMode::ReplName;
            prompt_.emplace("Run REPL: ");
            statusMessage_ = prompt_->StatusText();
            return;
        // test-runner integration: one-shot direct actions (no prompt --
        // one project-wide test command, see the enum's own comment).
        case editor::InteractiveRequest::RunTests: {
            if (!testRunner_) {
                statusMessage_ = "No test runner available.";
                return;
            }
            const bool alreadyRunning = testRunner_->IsRunning();
            if (text::Buffer* buffer = testRunner_->RunAll()) {
                activeBuffer_.Set(*buffer);
            }
            statusMessage_ = alreadyRunning ? "Tests already running." : (testRunner_->IsRunning() ? "Running tests..." : "");
            return;
        }
        case editor::InteractiveRequest::CancelTests:
            if (!testRunner_) {
                statusMessage_ = "No test runner available.";
            }
            else if (testRunner_->IsRunning()) {
                testRunner_->Cancel();
                statusMessage_ = "Cancelling tests...";
            }
            else {
                statusMessage_ = "No test run in progress.";
            }
            return;
        case editor::InteractiveRequest::ShowTestResults: {
            if (!testRunner_) {
                statusMessage_ = "No test runner available.";
                return;
            }
            const std::optional<editor::testrun::TestRunOutcome>& outcome = testRunner_->LatestOutcome();
            if (!outcome) {
                statusMessage_ = "No test results yet -- run-tests (C-c T t) first.";
                return;
            }
            editor::SetLastResultsBuffer(editor::testrun::TestResultsBufferName());
            activeBuffer_.Set(editor::testrun::RebuildTestResultsBuffer(bufferList_, *outcome));
            return;
        }
        case editor::InteractiveRequest::RunTestAtPoint: {
            if (!TestRunPreconditionsMet()) {
                return;
            }
            text::Buffer& buffer = activeBuffer_.Get();
            // Innermost (smallest-span) discovered definition containing
            // point -- a describe() picks the it() under point, a PHPUnit
            // class its method.
            const std::size_t                 point = buffer.Point();
            std::optional<editor::TestMarker> target;
            for (editor::TestMarker& marker : mode_.testDiscovery(buffer.Text())) {
                if (marker.startByte <= point && point < marker.endByte &&
                    (!target || (marker.endByte - marker.startByte) < (target->endByte - target->startByte))) {
                    target = std::move(marker);
                }
            }
            if (!target) {
                statusMessage_ = "No test definition at point.";
                return;
            }
            RunSingleTest(target->name);
            return;
        }
        case editor::InteractiveRequest::RerunFailedTests: {
            if (!testRunner_) {
                statusMessage_ = "No test runner available.";
                return;
            }
            if (testRunner_->IsRunning()) {
                statusMessage_ = "Tests already running.";
                return;
            }
            if (!editor::testrun::TestFilterCommand()) {
                statusMessage_ = "No test filter command configured (see ned/set-test-filter-command).";
                return;
            }
            const std::size_t queued = testRunner_->RerunFailed();
            if (queued == 0) {
                statusMessage_ = "No failed tests to re-run.";
                return;
            }
            if (text::Buffer* output = bufferList_.Find(editor::testrun::TestOutputBufferName())) {
                activeBuffer_.Set(*output);
            }
            statusMessage_ = "Re-running " + std::to_string(queued) + " failed test" + (queued == 1 ? "" : "s") +
                             " sequentially...";
            return;
        }
        // DAP client slice 1: one-shot direct actions, same shape as
        // VcsShowBlame just below -- the synchronous half of each answer
        // (Manager's returned status string) lands in statusMessage_
        // immediately; async outcomes (a breakpoint hit, the session
        // ending) arrive later through the WindowManager-wired
        // SetOnStopped/SetOnSessionEnded callbacks, not here.
        case editor::InteractiveRequest::DapContinue:
            statusMessage_ = dapManager_ ? dapManager_->StartOrContinue(editor::LanguageKeyForMode(mode_)) : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapAttach:
            statusMessage_ = dapManager_ ? dapManager_->Attach(editor::LanguageKeyForMode(mode_)) : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapStop:
            statusMessage_ = dapManager_ ? dapManager_->StopSession() : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapPause:
            statusMessage_ = dapManager_ ? dapManager_->Pause() : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapToggleBreakpoint: {
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
                return;
            }
            text::Buffer& buffer = activeBuffer_.Get();
            if (!buffer.Path()) {
                statusMessage_ = "Buffer has no file to set a breakpoint in.";
                return;
            }
            const std::size_t line   = buffer.Content().ByteOffsetToLine(buffer.Point()) + 1; // 1-based, DAP's own convention
            const bool        nowSet = dapManager_->ToggleBreakpoint(*buffer.Path(), line);
            statusMessage_           = (nowSet ? "Breakpoint set at " : "Breakpoint removed at ") + buffer.Path()->filename().string() +
                                       ":" + std::to_string(line);
            return;
        }
        // DAP slices 2/3: stepping is the same immediate-status shape as
        // DapContinue above; DapShowDebug/DapExpandVariable chain async
        // requests (see each method's own doc comment); DapEvaluate is the
        // family's one prompt session.
        case editor::InteractiveRequest::DapStepOver:
            statusMessage_ = dapManager_ ? dapManager_->StepOver() : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapStepInto:
            statusMessage_ = dapManager_ ? dapManager_->StepInto() : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapStepOut:
            statusMessage_ = dapManager_ ? dapManager_->StepOut() : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapShowDebug:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                ShowDebugInfo();
            }
            return;
        case editor::InteractiveRequest::DapExpandVariable:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                ExpandVariableAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapRestartFrame:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                RestartFrameAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapShowDisassembly:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                ShowDisassemblyAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapShowMemoryAtPoint:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                ShowMemoryAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapShowMemoryImageAtPoint:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                ShowMemoryImageAtPoint();
            }
            return;
        // Debugging wishlist: run-to-cursor -- same path/line resolution as
        // DapToggleBreakpoint above, forwarded straight to Manager (no
        // InputMode session, same as every other one-shot Dap* case).
        case editor::InteractiveRequest::DapRunToCursor: {
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
                return;
            }
            text::Buffer& buffer = activeBuffer_.Get();
            if (!buffer.Path()) {
                statusMessage_ = "Buffer has no file to run to.";
                return;
            }
            const std::size_t line = buffer.Content().ByteOffsetToLine(buffer.Point()) + 1; // 1-based, DAP's own convention
            statusMessage_         = dapManager_->RunToCursor(*buffer.Path(), line);
            return;
        }
        // Debugging wishlist: jump-to-line -- same path/line resolution as
        // DapRunToCursor above, but async (Manager::JumpToLine chains
        // gotoTargets/goto): an immediate placeholder status, then the real
        // outcome lands in the callback, same shape as DapEvaluate/
        // ShowDebugInfo's own async status updates.
        case editor::InteractiveRequest::DapJumpToLine: {
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
                return;
            }
            text::Buffer& buffer = activeBuffer_.Get();
            if (!buffer.Path()) {
                statusMessage_ = "Buffer has no file to jump within.";
                return;
            }
            const std::size_t line = buffer.Content().ByteOffsetToLine(buffer.Point()) + 1;
            statusMessage_         = "Jumping to line...";
            dapManager_->JumpToLine(*buffer.Path(), line, [this](bool, std::string message) { statusMessage_ = std::move(message); });
            return;
        }
        case editor::InteractiveRequest::DapToggleHexFormat:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                ToggleHexFormatAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapToggleWatchGraph:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                ToggleWatchGraphAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapLineInspect:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                LineInspectAtPoint();
            }
            return;
        // Debugging wishlist: reverse debugging -- same immediate-status
        // shape as DapStepOver/DapStepInto/DapStepOut above.
        case editor::InteractiveRequest::DapReverseContinue:
            statusMessage_ = dapManager_ ? dapManager_->ReverseContinue() : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapStepBack:
            statusMessage_ = dapManager_ ? dapManager_->StepBack() : "No debugger available.";
            return;
        case editor::InteractiveRequest::DapShowPointerGraph:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                RequestPointerGraphAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapAskAgentAboutState:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else if (dapManager_->State() != editor::dap::Manager::SessionState::Stopped) {
                statusMessage_ = "Debug session is not stopped.";
            }
            else if (!acpManager_ || acpManager_->State() != editor::acp::AcpManager::SessionState::Active) {
                statusMessage_ = "No active ACP session (see acp-start-session).";
            }
            else {
                SendDebugStateToAgent();
            }
            return;
        case editor::InteractiveRequest::AcpAskAgentAboutLine:
            if (!acpManager_ || acpManager_->State() != editor::acp::AcpManager::SessionState::Active) {
                statusMessage_ = "No active ACP session (see acp-start-session).";
            }
            else {
                SendResultLineToAgent();
            }
            return;
        case editor::InteractiveRequest::DapEvaluate:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
                return;
            }
            inputMode_ = InputMode::DapEvaluate;
            prompt_.emplace("Evaluate: ");
            statusMessage_ = prompt_->StatusText();
            return;
        // DAP round 2: SetBreakpointCondition/LogMessage capture point's own
        // line (dap-toggle-breakpoint's own convention) into
        // pendingDapBreakpointTarget_ before entering their prompt --
        // HandlePromptKey's DapBreakpointCondition/DapBreakpointLogMessage/
        // DapBreakpointHitCondition (round 3) branches consume it on Enter.
        case editor::InteractiveRequest::DapSetBreakpointCondition:
        case editor::InteractiveRequest::DapSetBreakpointLogMessage:
        case editor::InteractiveRequest::DapSetBreakpointHitCondition: {
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
                return;
            }
            text::Buffer& buffer = activeBuffer_.Get();
            if (!buffer.Path()) {
                statusMessage_ = "Buffer has no file to set a breakpoint in.";
                return;
            }
            const std::size_t line      = buffer.Content().ByteOffsetToLine(buffer.Point()) + 1;
            pendingDapBreakpointTarget_ = PendingDapBreakpointTarget{.path = *buffer.Path(), .line = line};
            switch (request) {
                case editor::InteractiveRequest::DapSetBreakpointCondition:
                    inputMode_ = InputMode::DapBreakpointCondition;
                    prompt_.emplace("Condition (empty to clear): ");
                    break;
                case editor::InteractiveRequest::DapSetBreakpointLogMessage:
                    inputMode_ = InputMode::DapBreakpointLogMessage;
                    prompt_.emplace("Log message (empty to clear): ");
                    break;
                default: // DapSetBreakpointHitCondition
                    inputMode_ = InputMode::DapBreakpointHitCondition;
                    prompt_.emplace("Hit condition (empty to clear): ");
                    break;
            }
            statusMessage_ = prompt_->StatusText();
            return;
        }
        case editor::InteractiveRequest::DapToggleFunctionBreakpoint:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
                return;
            }
            inputMode_ = InputMode::DapFunctionBreakpointName;
            prompt_.emplace("Function breakpoint name: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::DapSelectExceptionBreakpoints:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                BeginDapExceptionFilterSelect();
            }
            return;
        case editor::InteractiveRequest::DapAddWatch:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
                return;
            }
            inputMode_ = InputMode::DapAddWatch;
            prompt_.emplace("Add watch: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::DapRemoveWatch:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                RemoveWatchAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapSelectThread:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                BeginDapThreadSelect();
            }
            return;
        case editor::InteractiveRequest::DapSetVariable:
            if (!dapManager_) {
                statusMessage_ = "No debugger available.";
            }
            else {
                SetVariableAtPoint();
            }
            return;
        case editor::InteractiveRequest::DapToggleConsole:
            // The debug console is an OverlayHost overlay above even
            // WindowManager's level, same shape as ToggleTerminal/
            // AcpTogglePanel above -- only forward.
            if (onDapConsoleToggle_) {
                onDapConsoleToggle_();
            }
            return;
        case editor::InteractiveRequest::DapToggleThreadsPanel:
            // DapToggleConsole's own shape -- another OverlayHost overlay
            // above WindowManager's level, only forward.
            if (onDapThreadsToggle_) {
                onDapThreadsToggle_();
            }
            return;
        case editor::InteractiveRequest::ShowMassifGraph:
            inputMode_ = InputMode::ShowMassifGraphPath;
            prompt_.emplace("Massif output file: ");
            statusMessage_ = prompt_->StatusText();
            return;
        // ACP client slice 2: AcpStartSession/AcpSendPrompt are prompt-shaped
        // (HandlePromptKey), same "just enter the mode and prime the
        // prompt" shape as DapEvaluate above; AcpStopSession is a one-shot
        // direct action, same shape as DapStop.
        case editor::InteractiveRequest::AcpStartSession:
            inputMode_ = InputMode::AcpAgentName;
            prompt_.emplace("ACP agent: ");
            acpAgentNameList_.SelectTop();
            RefreshAcpAgentNameStatus();
            return;
        case editor::InteractiveRequest::AcpSendPrompt:
            if (!acpManager_) {
                statusMessage_ = "No ACP manager available.";
                return;
            }
            inputMode_ = InputMode::AcpPromptText;
            prompt_.emplace("ACP prompt: ");
            statusMessage_ = prompt_->StatusText();
            return;
        case editor::InteractiveRequest::AcpStopSession:
            statusMessage_ = acpManager_ ? acpManager_->StopSession() : "No ACP manager available.";
            return;
        case editor::InteractiveRequest::AcpTogglePanel:
            // ACP chat panel: one-shot direct action, same shape as
            // ToggleTerminal above -- the panel lives above this class,
            // only forward.
            if (onAcpPanelToggle_) {
                onAcpPanelToggle_();
            }
            return;
        case editor::InteractiveRequest::AcpRewind:
            // ACP checkpoint/rewind follow-up: one-shot direct action, same
            // "just forward, the target lives above this class" shape as
            // AcpTogglePanel immediately above -- the picker itself lives in
            // AcpPanel. Refused while a prompt is in flight: the agent could
            // still be mid-write, and rewinding out from under that would
            // race the very undo-sequence bookkeeping RewindTo relies on.
            if (!acpManager_) {
                statusMessage_ = "No ACP manager available.";
            }
            else if (acpManager_->PromptInFlight()) {
                statusMessage_ = "Can't rewind while a prompt is in flight.";
            }
            else if (onAcpRewindRequest_) {
                onAcpRewindRequest_();
            }
            return;
        // VCS blame gutter follow-up: one-shot direct actions, same shape
        // as ProjectAgenda/LspGotoDefinition above -- doesn't touch
        // inputMode_, the async result (or a status-message error) arrives
        // later via VcsRunner's own callback. VcsShowBlame stays on the
        // current buffer (see Command.h's own doc comment for why);
        // VcsBlameDetailAtPoint is synchronous, no async request at all.
        case editor::InteractiveRequest::VcsShowBlame:
            RequestBlameForCurrentBuffer();
            return;
        case editor::InteractiveRequest::VcsBlameDetailAtPoint:
            ShowBlameDetailAtPoint();
            return;
        case editor::InteractiveRequest::VcsBlameBuffer:
            RequestVcsBlameBuffer();
            return;
        case editor::InteractiveRequest::VcsShowLog:
            RequestVcsLogBuffer();
            return;
        case editor::InteractiveRequest::VisitVcsResult:
            VisitVcsResult();
            return;
        // VCS vocabulary-completion follow-up: status/stage/unstage/
        // branches are one-shot direct actions (async results via
        // VcsRunner callbacks, same as VcsShowLog above); commit and
        // create-branch open a prompt directly; switch-branch defers its
        // prompt until the branch list arrives (see
        // BeginVcsSwitchBranchPrompt's own doc comment).
        case editor::InteractiveRequest::VcsStatus:
            RequestVcsStatusBuffer();
            return;
        case editor::InteractiveRequest::VcsStageFile:
            StageOrUnstageFileAtPoint(true);
            return;
        case editor::InteractiveRequest::VcsUnstageFile:
            StageOrUnstageFileAtPoint(false);
            return;
        case editor::InteractiveRequest::VcsStageHunk:
            StageOrUnstageHunkAtPoint(true);
            return;
        case editor::InteractiveRequest::VcsUnstageHunk:
            StageOrUnstageHunkAtPoint(false);
            return;
        // Hunk-navigation follow-up: pure point motion against the
        // already-cached diffHunkStartLines_, no VcsRunner round trip and
        // no Modified() gate (see JumpToNextHunk/JumpToPreviousHunk's own
        // doc comments).
        case editor::InteractiveRequest::VcsNextHunk:
            JumpToNextHunk();
            return;
        case editor::InteractiveRequest::VcsPreviousHunk:
            JumpToPreviousHunk();
            return;
        // next-error follow-up: same point-motion shape as VcsNextHunk/
        // VcsPreviousHunk just above, generalized over Editor/NextError.h's
        // "last results buffer" instead of a private per-pane cache.
        case editor::InteractiveRequest::NextError:
            NextError();
            return;
        case editor::InteractiveRequest::PreviousError:
            PreviousError();
            return;
        case editor::InteractiveRequest::VcsFullDiffBuffer:
            RequestVcsFullDiffBuffer();
            return;
        case editor::InteractiveRequest::DiagnosticsBuffer:
            RequestDiagnosticsBuffer();
            return;
        case editor::InteractiveRequest::ProjectFindReferences:
            RequestProjectFindReferences();
            return;
        case editor::InteractiveRequest::VcsCommit:
            BeginVcsCommitMessage();
            return;
        case editor::InteractiveRequest::VcsCommitFinish:
            FinishVcsCommitMessage();
            return;
        case editor::InteractiveRequest::VcsCommitAbort:
            AbortVcsCommitMessage();
            return;
        case editor::InteractiveRequest::VcsBranches:
            RequestVcsBranchesBuffer();
            return;
        case editor::InteractiveRequest::VcsSwitchBranch:
            BeginVcsSwitchBranchPrompt();
            return;
        case editor::InteractiveRequest::VcsCreateBranch:
            BeginVcsCreateBranchPrompt();
            return;
        // org-set-tags follow-up: org-set-tags already checked
        // HeadlineAtPoint before setting this request, but point can't
        // have moved since then (no other command runs between a
        // command's own dispatch and StartInteractiveSession) -- resolving
        // it again here, rather than threading it through
        // InteractiveRequest somehow, is what lets the prompt pre-fill
        // with the headline's *current* tags without adding any new
        // payload to CommandContext.
        case editor::InteractiveRequest::SetHeadlineTags: {
            inputMode_          = InputMode::SetHeadlineTags;
            const auto headline = editor::org::HeadlineAtPoint(activeBuffer_.Get());
            prompt_.emplace("Tags (colon-separated): ");
            if (headline && !headline->tags.empty()) {
                std::string joined;
                for (const std::string& tag : headline->tags) {
                    if (!joined.empty())
                        joined += ':';
                    joined += tag;
                }
                prompt_->SetText(joined);
            }
            statusMessage_ = prompt_->StatusText();
            return;
        }
        // property-drawers follow-up: the first stage of org-set-property's
        // two-stage session -- see HandleSetPropertyKey for the second.
        case editor::InteractiveRequest::SetProperty:
            inputMode_     = InputMode::SetProperty;
            propertyStage_ = PropertyPromptStage::EnteringName;
            prompt_.emplace("Property name: ");
            statusMessage_ = prompt_->StatusText();
            return;
        // org-delete-property already checked HeadlineAtPoint before setting
        // this request (org-set-tags's own precedent) -- one prompt, fits
        // the shared HandlePromptKey else-chain directly.
        case editor::InteractiveRequest::DeleteProperty:
            inputMode_ = InputMode::DeleteProperty;
            prompt_.emplace("Delete property: ");
            statusMessage_ = prompt_->StatusText();
            return;
        // scheduling/recurrence follow-up: org-schedule/org-deadline already
        // checked HeadlineAtPoint before setting this request (org-set-tags's
        // own precedent) -- resolving it again here (point can't have moved
        // since dispatch, same reasoning SetHeadlineTags's own case above
        // states) is what lets the prompt pre-fill with the headline's
        // *current* SCHEDULED:/DEADLINE: timestamp, if it has one.
        case editor::InteractiveRequest::OrgSchedule:
        case editor::InteractiveRequest::OrgDeadline: {
            const bool isDeadline = (request == editor::InteractiveRequest::OrgDeadline);
            inputMode_            = isDeadline ? InputMode::OrgDeadline : InputMode::OrgSchedule;
            prompt_.emplace(isDeadline ? "Deadline: " : "Schedule: ");
            const auto headline = editor::org::HeadlineAtPoint(activeBuffer_.Get());
            if (headline) {
                const auto  planning = editor::org::ParsePlanning(activeBuffer_.Get().Text(), *headline);
                const auto& existing = isDeadline ? (planning ? planning->deadline : std::nullopt)
                                                  : (planning ? planning->scheduled : std::nullopt);
                if (existing) {
                    prompt_->SetText(editor::org::FormatTimestamp(*existing));
                }
            }
            statusMessage_ = prompt_->StatusText();
            return;
        }
        // org-capture follow-up: same one-character-read shape as
        // PointToRegister/etc. above -- no MinibufferPrompt, the status line
        // itself lists every registered template so the key isn't something
        // the user has to memorize blind.
        case editor::InteractiveRequest::OrgCapture: {
            const std::vector<editor::org::CaptureTemplate> templates = editor::org::CaptureTemplates();
            if (templates.empty()) {
                statusMessage_ = "No capture templates configured.";
                return;
            }
            std::string label = "Capture: ";
            for (const editor::org::CaptureTemplate& tmpl : templates) {
                label += "[";
                label += tmpl.key;
                label += "] " + tmpl.name + "  ";
            }
            inputMode_     = InputMode::OrgCaptureSelectTemplate;
            statusMessage_ = label;
            return;
        }
        case editor::InteractiveRequest::ExecuteCommand:
            // Deviates from the other cases' bare-label shape: an
            // immediately-visible, browsable candidate list is central to
            // this feature (unlike FindFile/SwitchToBuffer/etc., where
            // there's no meaningful "top" completion to show before any
            // input, or it'd mean an eager filesystem scan) -- so this
            // populates the ranked list (empty query -> every registered
            // command, alphabetical) right away via RefreshExecuteCommandStatus
            // rather than just prompt_->StatusText().
            inputMode_ = InputMode::ExecuteCommand;
            prompt_.emplace("M-x ");
            executeCommandList_.SelectTop();
            RefreshExecuteCommandStatus();
            return;
        // project-find-file follow-up: same "populate and show the full
        // candidate list right away" shape as ExecuteCommand just above,
        // but the candidate list is a real recursive directory walk
        // (editor::BuildProjectTree), not a free in-memory lookup -- done
        // once here, up front, rather than per keystroke (see
        // projectFindFileCandidates_'s own doc comment in BufferView.h). A
        // project with no files at all is a degenerate case with nothing to
        // pick from, so it's reported directly rather than opening a prompt
        // session over an empty list.
        case editor::InteractiveRequest::ProjectFindFile: {
            std::vector<std::string>    projectFiles;
            const std::filesystem::path root = editor::ProjectRoot();
            for (const editor::ProjectTreeEntry& entry : editor::BuildProjectTree(root)) {
                if (!entry.isDirectory) {
                    projectFiles.push_back(std::filesystem::relative(entry.path, root).generic_string());
                }
            }
            projectFindFileList_.Reset(std::move(projectFiles));
            if (projectFindFileList_.Empty()) {
                statusMessage_ = "No files found under " + root.string();
                return;
            }
            inputMode_ = InputMode::ProjectFindFile;
            prompt_.emplace("Find file (fuzzy): ");
            projectFindFileList_.SelectTop();
            RefreshProjectFindFileStatus();
            return;
        }
        // editor-ergonomics follow-up: ProjectFindFile's own "populate and
        // show the full candidate list right away" shape, over
        // editor::RecentFilePaths() (a cheap in-memory read, not a
        // directory walk) instead of BuildProjectTree.
        case editor::InteractiveRequest::FindRecentFile: {
            recentFileList_.Reset(editor::RecentFilePaths());
            if (recentFileList_.Empty()) {
                statusMessage_ = "No recently opened files";
                return;
            }
            inputMode_ = InputMode::FindRecentFile;
            prompt_.emplace("Find recent file (fuzzy): ");
            recentFileList_.SelectTop();
            RefreshFindRecentFileStatus();
            return;
        }
        // named-projects follow-up: ProjectFindFile/FindRecentFile's own
        // "populate and show the full candidate list right away" shape.
        case editor::InteractiveRequest::SwitchProject: {
            switchProjectEntries_ = editor::ListProjects();
            if (switchProjectEntries_.empty()) {
                statusMessage_ = "No registered projects yet -- try open-project.";
                return;
            }
            inputMode_ = InputMode::SwitchProject;
            prompt_.emplace("Switch to project (fuzzy): ");
            switchProjectList_.SelectTop();
            RefreshSwitchProjectStatus();
            return;
        }
        // named-projects follow-up: FindFile's own plain path-entry prompt
        // shape -- Enter (HandlePromptKey's own OpenProjectPath branch)
        // decides whether a second (name) prompt is needed.
        case editor::InteractiveRequest::OpenProject:
            inputMode_ = InputMode::OpenProjectPath;
            prompt_.emplace("Open project (path): ");
            pathCompletionSelection_ = 0;
            RefreshPathCompletionPopup();
            return;
        // editor-ergonomics follow-up: bookmark-set already checked
        // context.buffer.Path() before setting this (Commands.cpp), so
        // activeBuffer_.Get() is guaranteed file-backed here -- pre-fills
        // with the buffer's own display name, the common case (one
        // bookmark per file) needing no typing at all, just Enter.
        case editor::InteractiveRequest::BookmarkSet:
            inputMode_ = InputMode::BookmarkSetName;
            prompt_.emplace("Bookmark name: ");
            prompt_->SetText(activeBuffer_.Get().Name());
            statusMessage_ = prompt_->StatusText();
            return;
        // editor-ergonomics follow-up: BookmarkJump/BookmarkDelete share
        // one picker (InputMode::BookmarkJump) over editor::BookmarkNames(),
        // TaskName's own RunTask/CancelTask precedent for two
        // InteractiveRequests resolving to the same InputMode.
        case editor::InteractiveRequest::BookmarkJump:
        case editor::InteractiveRequest::BookmarkDelete: {
            const bool isDelete = (request == editor::InteractiveRequest::BookmarkDelete);
            bookmarkList_.Reset(editor::BookmarkNames());
            if (bookmarkList_.Empty()) {
                statusMessage_ = "No bookmarks set";
                return;
            }
            bookmarkPromptAction_ = isDelete ? BookmarkPromptAction::Delete : BookmarkPromptAction::Jump;
            inputMode_            = InputMode::BookmarkJump;
            prompt_.emplace(isDelete ? "Delete bookmark (fuzzy): " : "Jump to bookmark (fuzzy): ");
            bookmarkList_.SelectTop();
            RefreshBookmarkJumpStatus();
            return;
        }
        // rich-theme-set follow-up (Phase 1): ProjectFindFile's "populate
        // and show the full candidate list right away" shape over
        // ui::ThemeNames(). No applier wired (headless BufferView tests
        // that never call SetThemeApplier) means there's nothing a picked
        // theme could be applied *to*, so report instead of opening a
        // session whose Enter would silently do nothing.
        //
        // select-theme-current-row follow-up: selectThemeCandidates_ gets
        // one synthetic entry (kCurrentThemeLabel) prepended ahead of every
        // real registry name, and the session opens on it -- so the very
        // top of the list, always, rather than trying to locate the active
        // theme's own row among the real ones (which meant either
        // previewing it immediately on open, a destructive no-op that
        // strips init.janet's own overrides -- see kCurrentThemeLabel's own
        // doc comment -- or leaving a highlighted-but-unapplied row that
        // looked like nothing had happened). Opening the picker now
        // previews nothing and touches theme_ not at all, full stop.
        case editor::InteractiveRequest::SelectTheme: {
            if (!themeApplier_) {
                statusMessage_ = "Theme switching is not wired up.";
                return;
            }
            std::vector<std::string> themeNames = ThemeNames();
            themeNames.insert(themeNames.begin(), std::string(kCurrentThemeLabel));
            selectThemeList_.Reset(std::move(themeNames));
            themeBeforePreview_ = theme_;
            inputMode_          = InputMode::SelectTheme;
            prompt_.emplace("Theme (fuzzy): ");
            selectThemeList_.SelectTop();
            RefreshSelectThemeStatus();
            return;
        }
        // theme-editing follow-up: one-shot direct action, ToggleProjectSidebar's
        // shape. Writes whatever theme is *currently showing* -- picker-
        // committed, ned/set-theme'd, override-adjusted, or the ANSI
        // fallback -- as runnable Janet, so "pick something close, save it,
        // edit the file" is the whole theme-authoring workflow.
        case editor::InteractiveRequest::SaveTheme:
            try {
                const std::filesystem::path path = ThemeJanetFilePath();
                SaveThemeJanetFile(theme_, path);
                statusMessage_ = "Saved theme to " + path.string();
            }
            catch (const std::exception& e) {
                ReportError(e.what());
            }
            return;
        // kmacro-start-macro/kmacro-end-or-call-macro follow-up: one-shot
        // direct actions, same shape as ToggleProjectSidebar -- inputMode_
        // stays Normal, no prompt session. The actual recording state lives
        // on dispatcher_ (Dispatcher::StartRecording/StopRecording/
        // IsRecording/LastMacro), not here.
        case editor::InteractiveRequest::StartKbdMacro:
            dispatcher_.StartRecording();
            statusMessage_ = "Recording keyboard macro...";
            return;
        case editor::InteractiveRequest::EndOrCallKbdMacro:
            if (dispatcher_.IsRecording()) {
                // This very keypress's own chord(s) were just appended to
                // the in-progress recording by Feed (recording_ was still
                // true throughout that call) -- strip them back out before
                // finalizing, so the macro's own terminator never ends up
                // inside it. See Dispatcher::DiscardMostRecentlyRecordedChords's
                // own doc comment for why this has to happen here, not
                // inside Dispatcher::Feed/StopRecording themselves.
                dispatcher_.DiscardMostRecentlyRecordedChords();
                dispatcher_.StopRecording();
                statusMessage_ =
                    "Keyboard macro recorded (" + std::to_string(dispatcher_.LastMacro().size()) + " keys).";
            }
            else {
                ReplayMacro();
            }
            return;
        // point-to-register/jump-to-register/copy-to-register/insert-register
        // follow-up: each just waits for one more character (the register
        // name) via the shared HandleRegisterKey -- no MinibufferPrompt,
        // there's nothing to accumulate, just a bare label like every other
        // prompt-shaped case here uses.
        case editor::InteractiveRequest::PointToRegister:
            inputMode_     = InputMode::PointToRegister;
            statusMessage_ = "Point to register: ";
            return;
        case editor::InteractiveRequest::JumpToRegister:
            inputMode_     = InputMode::JumpToRegister;
            statusMessage_ = "Jump to register: ";
            return;
        case editor::InteractiveRequest::CopyToRegister:
            inputMode_     = InputMode::CopyToRegister;
            statusMessage_ = "Copy to register: ";
            return;
        case editor::InteractiveRequest::InsertRegister:
            inputMode_     = InputMode::InsertRegister;
            statusMessage_ = "Insert register: ";
            return;
        // Emacs-keymap-round-2 follow-up: same one-character-read shape as
        // the register requests just above.
        case editor::InteractiveRequest::ZapToChar:
            inputMode_     = InputMode::ZapToChar;
            statusMessage_ = "Zap to char: ";
            return;
        // kill-rectangle/delete-rectangle/yank-rectangle follow-up: one-shot
        // direct actions, same shape as ToggleProjectSidebar -- inputMode_
        // stays Normal, no prompt session. See Editor/Rectangle.h for where
        // the actual operations live.
        case editor::InteractiveRequest::KillRectangle: {
            text::Buffer& buffer = activeBuffer_.Get();
            if (!buffer.HasSecondaryCursors()) {
                if (!buffer.HasMark()) {
                    statusMessage_ = "No rectangle region selected.";
                }
                else {
                    editor::KillRectangle(buffer, editor::TabWidth());
                    statusMessage_.clear();
                }
                return;
            }
            // multi-cursor-round-2 follow-up: each cursor already carries
            // its own optional mark, so KillRectangle itself needs no
            // change -- just run it per cursor and collect what it
            // published to the (single-slot) clipboard each time into one
            // multi-block entry afterward.
            std::vector<std::vector<std::string>> blocks;
            bool                                  any = false;
            buffer.ForEachCursor([&] {
                if (buffer.HasMark()) {
                    editor::KillRectangle(buffer, editor::TabWidth());
                    blocks.push_back(editor::GlobalRectangleClipboard().Lines());
                    any = true;
                }
                else {
                    blocks.emplace_back();
                }
            });
            if (any) {
                editor::SetRectangleClipboardBlocks(std::move(blocks));
                statusMessage_.clear();
            }
            else {
                statusMessage_ = "No rectangle region selected.";
            }
            return;
        }
        case editor::InteractiveRequest::DeleteRectangle: {
            text::Buffer& buffer = activeBuffer_.Get();
            if (!buffer.HasSecondaryCursors()) {
                if (!buffer.HasMark()) {
                    statusMessage_ = "No rectangle region selected.";
                }
                else {
                    editor::DeleteRectangle(buffer, editor::TabWidth());
                    statusMessage_.clear();
                }
                return;
            }
            bool any = false;
            buffer.ForEachCursor([&] {
                if (buffer.HasMark()) {
                    editor::DeleteRectangle(buffer, editor::TabWidth());
                    any = true;
                }
            });
            statusMessage_ = any ? std::string() : "No rectangle region selected.";
            return;
        }
        case editor::InteractiveRequest::YankRectangle: {
            text::Buffer& buffer = activeBuffer_.Get();
            if (editor::GlobalRectangleClipboard().Empty()) {
                statusMessage_ = "No rectangle to yank.";
                return;
            }
            if (!buffer.HasSecondaryCursors()) {
                editor::YankRectangle(buffer, editor::TabWidth());
                statusMessage_.clear();
                return;
            }
            // multi-cursor-round-2 follow-up: 1:1 block-per-cursor when the
            // block count matches how many cursors are live right now, else
            // fall back to the single most-recent block (Lines()) at every
            // cursor -- narrower than KillRing/RegisterTable's own "join
            // into one blob" mismatch fallback, see RectangleClipboard's
            // own doc comment for why.
            const auto&       blocks      = editor::GlobalRectangleClipboard().Blocks();
            const std::size_t cursorCount = 1 + buffer.SecondaryCursors().size();
            const bool        perCursor   = blocks.size() == cursorCount;
            std::size_t       i           = 0;
            buffer.ForEachCursor([&] {
                editor::YankRectangleLines(buffer, perCursor ? blocks[i] : editor::GlobalRectangleClipboard().Lines(),
                                           editor::TabWidth());
                ++i;
            });
            statusMessage_.clear();
            return;
        }
        // string-rectangle follow-up: the one rectangle command that's a
        // real prompt session (needs one line of typed replacement text) --
        // HasMark() is checked here, before ever opening the prompt, so
        // there's nothing to cancel out of if there's no region at all.
        // multi-cursor-round-2 follow-up: with secondary cursors active,
        // this optimistically opens the prompt without pre-scanning every
        // cursor for a mark (that scan would need its own ForEachCursor
        // pass just to answer a yes/no gate) -- if it turns out none of
        // them have one, the confirm handler below silently does nothing,
        // a narrow, accepted v1 gap rather than a full pre-check.
        case editor::InteractiveRequest::StringRectangle:
            if (!activeBuffer_.Get().HasMark() && !activeBuffer_.Get().HasSecondaryCursors()) {
                statusMessage_ = "No rectangle region selected.";
            }
            else {
                inputMode_ = InputMode::StringRectangle;
                prompt_.emplace("String rectangle: ");
                statusMessage_ = prompt_->StatusText();
            }
            return;
        // narrow-to-region/widen follow-up: one-shot direct actions, same
        // shape as ToggleProjectSidebar -- inputMode_ stays Normal, no
        // prompt session for either.
        case editor::InteractiveRequest::NarrowToRegion:
            if (!activeBuffer_.Get().HasMark()) {
                statusMessage_ = "No region to narrow to.";
            }
            else {
                const auto [start, end] = activeBuffer_.Get().Region();
                activeBuffer_.Get().NarrowToRegion(start, end);
                const std::size_t narrowedStart = activeBuffer_.Get().NarrowedRange().first;
                viewport_.SetTopLine(activeBuffer_.Get().Content().ByteOffsetToLine(narrowedStart));
                statusMessage_.clear();
            }
            return;
        case editor::InteractiveRequest::Widen:
            activeBuffer_.Get().Widen();
            statusMessage_.clear();
            return;
        // structural-selection-expansion follow-up: one-shot direct actions,
        // same shape as NarrowToRegion/ToggleProjectSidebar above.
        case editor::InteractiveRequest::ExpandSelection: {
            if (!mode_.expandSelection) {
                statusMessage_ = "No structural selection support in this mode.";
                return;
            }
            text::Buffer& buffer = activeBuffer_.Get();
            if (expansionHistoryBuffer_ != &buffer || expansionHistoryGeneration_ != buffer.ContentGeneration()) {
                expansionHistory_.clear();
            }
            const auto [startByte, endByte]                                   = buffer.HasMark() ? buffer.Region() : std::pair{buffer.Point(), buffer.Point()};
            const std::optional<std::pair<std::size_t, std::size_t>> expanded = mode_.expandSelection(buffer.Text(), startByte, endByte);
            if (!expanded) {
                statusMessage_ = "Already at outermost node.";
                return;
            }
            expansionHistory_.emplace_back(startByte, endByte);
            buffer.SetMark(expanded->first);
            buffer.SetPoint(expanded->second);
            expansionHistoryBuffer_     = &buffer;
            expansionHistoryGeneration_ = buffer.ContentGeneration();
            statusMessage_.clear();
            return;
        }
        case editor::InteractiveRequest::ShrinkSelection: {
            text::Buffer& buffer = activeBuffer_.Get();
            const bool    stale =
                expansionHistoryBuffer_ != &buffer || expansionHistoryGeneration_ != buffer.ContentGeneration() || expansionHistory_.empty();
            if (stale) {
                statusMessage_ = "No selection to shrink to.";
                return;
            }
            const auto [startByte, endByte] = expansionHistory_.back();
            expansionHistory_.pop_back();
            buffer.SetMark(startByte);
            buffer.SetPoint(endByte);
            expansionHistoryGeneration_ = buffer.ContentGeneration();
            statusMessage_.clear();
            return;
        }
        case editor::InteractiveRequest::None:
            return;
        // Window-splitting follow-up: structural, operate above the level
        // of a single BufferView -- just forward to whoever registered
        // SetOnWindowRequest (WindowManager), the same "signal intent, host
        // acts on it" shape every InteractiveRequest already uses. inputMode_
        // deliberately stays Normal -- these aren't interactive sessions.
        case editor::InteractiveRequest::SplitBelow:
        case editor::InteractiveRequest::SplitRight:
        case editor::InteractiveRequest::DeleteWindow:
        case editor::InteractiveRequest::DeleteOtherWindows:
        case editor::InteractiveRequest::OtherWindow:
        // Split-resize follow-up: same forward-only shape as the five
        // above -- unlike those, none of these ever reshape the tree (a
        // resize only mutates a WindowNode's own ratio), so `this` stays
        // valid afterward and these are deliberately NOT part of
        // IsWindowManagementRequest's own "the pane may be gone" check.
        case editor::InteractiveRequest::EnlargeWindow:
        case editor::InteractiveRequest::ShrinkWindow:
        case editor::InteractiveRequest::EnlargeWindowHorizontally:
        case editor::InteractiveRequest::ShrinkWindowHorizontally:
            if (onWindowRequest_) {
                onWindowRequest_(request);
            }
            return;
        // Links follow-up: a one-shot direct action, same shape as
        // VisitSearchResult -- doesn't touch inputMode_.
        case editor::InteractiveRequest::OpenLinkAtPoint:
            OpenLinkAtPoint();
            return;
        // hover/completion follow-up: another one-shot direct action --
        // doesn't touch inputMode_, completion state coexists with ordinary
        // Normal-mode editing rather than replacing it (see
        // ActiveCompletion's own doc comment in BufferView.h).
        case editor::InteractiveRequest::LspComplete:
            RequestCompletionAtPoint();
            return;
        // documentHighlight follow-up: manual M-x entry point into the same
        // RequestDocumentHighlightAtPoint the live-on-cursor-move path (see
        // MaybeScheduleDocumentHighlight) already drives.
        case editor::InteractiveRequest::LspDocumentHighlight:
            RequestDocumentHighlightAtPoint();
            return;
        // code-actions follow-up: also a one-shot direct action -- inputMode_
        // is deliberately left untouched here, only changed later, from
        // inside RequestCodeActionsAtPoint's own async callback once the
        // response actually arrives (see that method's own doc comment).
        case editor::InteractiveRequest::LspCodeAction:
            RequestCodeActionsAtPoint();
            return;
        // quick-fix follow-up: same one-shot shape as LspCodeAction just
        // above; only enters an InputMode when the fix choice turns out to
        // be genuinely ambiguous (see RequestQuickFixAtPoint's doc comment).
        case editor::InteractiveRequest::LspQuickFix:
            RequestQuickFixAtPoint();
            return;
        // codeLens follow-up: same one-shot shape as LspQuickFix just
        // above -- never enters an InputMode at all.
        case editor::InteractiveRequest::LspRunCodeLensAtPoint:
            RequestCodeLensAtPoint();
            return;
    }

    statusMessage_ = (inputMode_ == InputMode::QueryReplace) ? queryReplace_->StatusText() : SearchStatusText();
}

std::string BufferView::SearchStatusText() const {
    std::string        text  = search_->StatusLabel();
    const std::string& query = search_->Query();

    // partial-match-highlighting follow-up: a failing search's query is
    // split at MatchedPrefixLength() -- the still-matching prefix rendered
    // emphasized (bold), the broken remainder rendered in the same error
    // color the gutter uses for an LSP error -- so it's visible at a glance
    // both how much of what's typed still corresponds to real buffer
    // content and exactly which bytes broke it, rather than only learning
    // that from the "Failing " label. Falls back to the whole query plain
    // whenever there's nothing meaningful to split (a successful search, an
    // empty query, or MatchedPrefixLength() reporting "not available" for a
    // huge buffer -- see its own doc comment).
    const std::size_t matchedLen = search_->MatchedPrefixLength();
    if (search_->Found() || query.empty() || matchedLen >= query.size()) {
        text += query;
    }
    else {
        text += EmphasizeForEchoArea(query.substr(0, matchedLen));
        text += ErrorForEchoArea(query.substr(matchedLen));
    }

    if (query.empty() && !lastSearchQuery_.empty()) {
        text += GhostForEchoArea(lastSearchQuery_);
    }
    return text;
}

void BufferView::BeginSnippetExpansion(std::size_t replaceStart, std::size_t replaceEnd, const std::string& body) {
    // Any prior session's ranges must never leak into a fresh expansion
    // (unreachable through TAB, which a live session consumes, but the LSP
    // accept path and M-x expand-snippet land here too).
    EndSnippetSession();
    // linked-editing-range follow-up: same storage collision reasoning --
    // see linkedEditingSession_'s own doc comment.
    EndLinkedEditingSession();
    text::Buffer& buffer  = activeBuffer_.Get();
    auto          session = editor::SnippetSession::Start(buffer, buffer.Name(), replaceStart, replaceEnd,
                                                          editor::ParseSnippet(body, BuildSnippetVariables(buffer)));
    if (session) {
        snippetSession_.emplace(std::move(*session));
        inputMode_     = InputMode::Snippet;
        statusMessage_ = snippetSession_->StatusText();
    }
    ClampPointToNarrowing();
    viewport_.ScrollToShowPoint();
}

text::Buffer* BufferView::ResolveSnippetBuffer() {
    if (!snippetSession_) {
        return nullptr;
    }
    if (text::Buffer* buffer = bufferList_.Find(snippetSession_->BufferName())) {
        return buffer;
    }
    if (activeBuffer_.Get().Name() == snippetSession_->BufferName()) {
        return &activeBuffer_.Get();
    }
    return nullptr;
}

void BufferView::EndSnippetSession() {
    if (snippetSession_) {
        if (text::Buffer* buffer = ResolveSnippetBuffer()) {
            buffer->ClearSnippetRanges();
        }
        snippetSession_.reset();
    }
    pendingSnippetExpansion_.reset();
    snippetPendingPristineDelete_ = false;
    if (inputMode_ == InputMode::Snippet) {
        inputMode_ = InputMode::Normal;
    }
}

void BufferView::EndInteractiveSession() {
    inputMode_ = InputMode::Normal;
    // generic-popup follow-up (Phase 3): unconditional -- hides whichever
    // candidate popup this session may have been driving, a safe no-op if
    // it wasn't (same tolerance SetOnPrefixHintChanged's own nullopt case
    // has).
    if (onCandidatesChanged_) {
        onCandidatesChanged_(std::nullopt);
    }
    // snippet-expansion follow-up: a snippet session ending through this
    // shared reset (any other session's own end path) clears its
    // buffer-side ranges too, not just the members.
    EndSnippetSession();
    // linked-editing-range follow-up: same reasoning, same place.
    EndLinkedEditingSession();
    search_.reset();
    queryReplace_.reset();
    prompt_.reset();
    promptHistoryIndex_ = kNoHistoryIndex;
    promptHistoryStash_.clear();
    projectReplace_.reset();
    pendingClose_ = nullptr;
    pendingBinaryOpenPath_.clear();
    pendingOpenProjectRoot_.clear(); // session state, cleared with the rest of it
    pendingZapToCharAppend_ = false;
    pendingTrustInitPath_.clear();
    onTrustDecision_ = nullptr;
    deleteStage_     = DeleteFileStage::EnteringPath;
    deleteTarget_.clear();
    renameStage_ = RenameFileStage::EnteringSource;
    renameSource_.clear();
    propertyStage_ = PropertyPromptStage::EnteringName;
    pendingPropertyName_.clear();
    executeCommandList_.SelectTop();
    // Both pools are cached only for the duration of one session.
    projectFindFileList_.Clear();
    selectThemeList_.Clear();
    // The cancel path re-applies this snapshot *before* calling here; the
    // commit path applies the selected theme instead and lets this drop.
    themeBeforePreview_.reset();
    pendingCodeActions_.clear();
    codeActionSelection_ = 0;
    pendingDefinitions_.clear();
    definitionSelection_ = 0;
    // peek-definition follow-up: unconditional, same tolerance
    // onCandidatesChanged_'s own reset above has -- hides the peek popup
    // whether or not this session ever showed it.
    if (onPeekChanged_) {
        onPeekChanged_(std::nullopt);
    }
    pendingPeekDefinitions_.clear();
    peekDefinitionSelection_ = 0;
    // right-click-context-menu follow-up: same unconditional tolerance
    // onPeekChanged_'s own reset above has -- hides the context menu popup
    // whether or not this session ever showed it.
    if (onContextMenuChanged_) {
        onContextMenuChanged_(std::nullopt);
    }
    contextMenuEntries_.clear();
    contextMenuSelection_ = 0;
    contextMenuAnchor_.reset();
    renameTitle_.clear();
    viewport_.ScrollToShowPoint();
}

void BufferView::HandleSearchKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        if (!search_->Query().empty()) {
            lastSearchQuery_ = search_->Query();
        }
        search_->Accept();
        EndInteractiveSession();
        return;
    }
    if (IsQuit(chord)) {
        if (!search_->Query().empty()) {
            lastSearchQuery_ = search_->Query();
        }
        search_->Cancel();
        EndInteractiveSession();
        return;
    }

    if (chord.Special == editor::SpecialKey::Backspace) {
        search_->DeleteChar();
    }
    else if (chord.Control && chord.Codepoint == U's') {
        // An empty query recalls the last search string outright (real
        // Emacs: C-s/C-r on a fresh isearch reuses the previous one, shown
        // ghosted via SearchStatusText() up to this point). Otherwise, C-s
        // while already searching forward repeats; while searching
        // backward it reverses direction instead (also real Emacs isearch
        // behavior) -- inputMode_ is kept in sync with search_'s own
        // direction since InIsearchMatch reads it to know which end of the
        // match point() is at.
        if (search_->Query().empty() && !lastSearchQuery_.empty()) {
            search_->AppendText(lastSearchQuery_);
        }
        else if (inputMode_ == InputMode::IsearchBackward) {
            search_->ReverseDirection();
            inputMode_ = InputMode::IsearchForward;
        }
        else {
            search_->RepeatSearch();
        }
    }
    else if (chord.Control && chord.Codepoint == U'r') {
        if (search_->Query().empty() && !lastSearchQuery_.empty()) {
            search_->AppendText(lastSearchQuery_);
        }
        else if (inputMode_ == InputMode::IsearchForward) {
            search_->ReverseDirection();
            inputMode_ = InputMode::IsearchBackward;
        }
        else {
            search_->RepeatSearch();
        }
    }
    else if (chord.Control && chord.Codepoint == U'w') {
        search_->AppendWordAtPoint();
    }
    else if (chord.Control && chord.Codepoint == U'y') {
        search_->AppendText(killRing_.Current());
    }
    else if (IsPlainCharacter(chord)) {
        search_->AppendChar(chord.Codepoint);
    }
    else {
        // isearch-motion-key-exits follow-up: real Emacs isearch behavior --
        // anything reaching this branch (arrows, Home/End/PageUp/PageDown,
        // and the plain Emacs motion chords -- C-f/C-b/C-n/C-p/C-a/C-e,
        // M-f/M-b/M-</M->, C-v/M-v, ...) isn't an isearch command, so rather
        // than being silently swallowed (this branch's old behavior), the
        // key ends the search (keeping the current match position, same as
        // Enter) and re-dispatches itself through the normal keymap,
        // applying whatever it would ordinarily do instead of requiring an
        // explicit Enter/Escape first. Deliberately unconditional -- every
        // isearch-specific chord (C-s/C-r/C-w/C-y, Backspace, a plain
        // character, Enter, Escape/C-g) was already claimed by an earlier
        // branch above, so nothing legitimate is lost by treating whatever
        // remains as "not ours."
        if (!search_->Query().empty()) {
            lastSearchQuery_ = search_->Query();
        }
        search_->Accept();
        EndInteractiveSession();
        DispatchChordNormally(chord);
        return;
    }

    statusMessage_ = SearchStatusText();
    viewport_.ScrollToShowPoint();
}

void BufferView::HandleQueryReplaceKey(const editor::KeyChord& chord) {
    // in-file-regex follow-up: any stage that *searches* (ConfirmReplacement's
    // first find, every y/n/! step) can throw RegexPatternError if PCRE2's
    // match-limit safety net trips on a catastrophically backtracking
    // pattern -- retrying the same key would just trip it again, so end the
    // session with the error visible rather than crash or loop.
    try {
        HandleQueryReplaceKeyInner(chord);
    }
    catch (const editor::RegexPatternError& e) {
        queryReplace_->Cancel();
        ReportError(std::string("Query replace: ") + e.what());
        EndInteractiveSession();
        return;
    }

    if (queryReplace_->CurrentStage() == editor::QueryReplace::Stage::Done) {
        EndInteractiveSession();
        return;
    }

    viewport_.ScrollToShowPoint();
}

std::string_view BufferView::HistoryKeyForInputMode(InputMode mode) {
    switch (mode) {
        case InputMode::FindFile:
            return "find-file";
        case InputMode::ProjectSearch:
            return "project-search";
        case InputMode::CreateDirectory:
            return "create-directory";
        case InputMode::FindScratch:
            return "find-scratch";
        case InputMode::GotoLine:
            return "goto-line";
        case InputMode::StringRectangle:
            return "string-rectangle";
        case InputMode::SetHeadlineTags:
            return "set-headline-tags";
        case InputMode::DeleteProperty:
            return "delete-property";
        case InputMode::OrgSchedule:
            return "org-schedule";
        case InputMode::OrgDeadline:
            return "org-deadline";
        case InputMode::LspRenameNewName:
            return "lsp-rename";
        case InputMode::TaskName:
            return "task-name";
        case InputMode::ReplName:
            return "repl-name";
        case InputMode::DapEvaluate:
            return "dap-evaluate";
        case InputMode::DapBreakpointCondition:
            return "dap-breakpoint-condition";
        case InputMode::DapBreakpointLogMessage:
            return "dap-breakpoint-log-message";
        case InputMode::DapAddWatch:
            return "dap-add-watch";
        case InputMode::DapSetVariableValue:
            return "dap-set-variable";
        case InputMode::DapBreakpointHitCondition:
            return "dap-breakpoint-hit-condition";
        case InputMode::DapFunctionBreakpointName:
            return "dap-function-breakpoint-name";
        case InputMode::DapMemoryByteCount:
            return "dap-memory-byte-count";
        case InputMode::ShowMassifGraphPath:
            return "show-massif-graph";
        case InputMode::VcsCreateBranch:
            return "vcs-create-branch";
        case InputMode::AcpPromptText:
            return "acp-prompt-text";
        case InputMode::BookmarkSetName:
            return "bookmark-set";
        case InputMode::OpenProjectPath:
            return "open-project-path";
        case InputMode::OpenProjectName:
            return "open-project-name";
        default:
            return "prompt"; // unreachable from HandlePromptKey's own dispatch guard; a safe shared fallback regardless
    }
}

bool BufferView::TryNavigatePromptHistory(const editor::KeyChord& chord, std::string_view key) {
    if (!chord.Meta || chord.Control) {
        return false;
    }
    if (chord.Codepoint != U'p' && chord.Codepoint != U'n') {
        return false;
    }

    // Deliberately never touches statusMessage_ itself, even at a history
    // boundary (no entries yet / already at the oldest or the live edit) --
    // every caller's own post-navigation refresh (plain StatusText() here,
    // a re-ranked fuzzy-candidate line in ExecuteCommand/ProjectFindFile)
    // runs unconditionally right after a true return, and there's no separate
    // echo-area slot to show a transient "no more history" note in without
    // that refresh immediately clobbering it -- so a boundary press is a
    // silent no-op instead, same as Backspace on an already-empty prompt.
    const std::vector<std::string>& entries = promptHistory_.Entries(key);

    if (chord.Codepoint == U'p') { // older
        if (promptHistoryIndex_ == kNoHistoryIndex) {
            if (entries.empty()) {
                return true;
            }
            promptHistoryStash_ = prompt_->Text();
            promptHistoryIndex_ = 0;
        }
        else if (promptHistoryIndex_ + 1 < entries.size()) {
            ++promptHistoryIndex_;
        }
        else {
            return true; // already at the oldest entry
        }
        prompt_->SetText(entries[promptHistoryIndex_]);
        return true;
    }

    // 'n', newer
    if (promptHistoryIndex_ == kNoHistoryIndex) {
        return true; // already at the live edit -- nothing to do
    }
    if (promptHistoryIndex_ > 0) {
        --promptHistoryIndex_;
        prompt_->SetText(entries[promptHistoryIndex_]);
    }
    else {
        prompt_->SetText(promptHistoryStash_);
        promptHistoryIndex_ = kNoHistoryIndex;
    }
    return true;
}

bufferview::PromptCompletion BufferView::PromptCompletionForCurrentMode() const {
    const std::optional<bufferview::TextEntryPrompt> prompt = TextEntryPromptFor(inputMode_);
    return prompt ? prompt->completion : bufferview::PromptCompletion::None;
}

std::optional<bufferview::TextEntryPrompt> BufferView::TextEntryPromptFor(InputMode mode) const {
    switch (mode) {
        // Real filesystem paths, with the dropdown Tab accepts from.
        case InputMode::FindFile:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::PathDropdown, "Find file"};
        case InputMode::OpenProjectPath:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::PathDropdown, "Open project"};
        case InputMode::FindScratch:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::PathDropdown, "Find scratch"};

        // Naming something after an existing buffer or file is common enough to
        // be worth completing against them.
        case InputMode::BookmarkSetName:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::Names, "Bookmark name"};
        case InputMode::OpenProjectName:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::Names, "Project name"};

        // The task prompt is the one whose label depends on state: the same
        // prompt runs a task or cancels one.
        case InputMode::TaskName:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None,
                                               taskPromptAction_ == TaskPromptAction::Run ? "Run task" : "Cancel task"};

        // Everything else is free text -- completing any of these against buffer
        // and file names would be meaningless: a search regex, a column of text,
        // org tags, a rename, a REPL name, a debuggee expression, a branch being
        // deliberately invented, a message to an agent, a property name, a date,
        // the DAP condition/log/watch/value/hit-count/function prompts, a byte
        // count, a massif output path, or a line number.
        case InputMode::AcpPromptText:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Send ACP prompt"};
        case InputMode::CreateDirectory:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Create directory"};
        case InputMode::DapAddWatch:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Add watch"};
        case InputMode::DapBreakpointCondition:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Breakpoint condition"};
        case InputMode::DapBreakpointHitCondition:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Breakpoint hit condition"};
        case InputMode::DapBreakpointLogMessage:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Breakpoint log message"};
        case InputMode::DapEvaluate:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Evaluate"};
        case InputMode::DapFunctionBreakpointName:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Function breakpoint name"};
        case InputMode::DapMemoryByteCount:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Memory byte count"};
        case InputMode::DapSetVariableValue:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Set variable"};
        case InputMode::DeleteProperty:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Delete property"};
        case InputMode::GotoLine:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Goto line"};
        case InputMode::LspRenameNewName:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Rename"};
        case InputMode::OrgDeadline:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Deadline"};
        case InputMode::OrgSchedule:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Schedule"};
        case InputMode::ProjectSearch:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Project search"};
        case InputMode::ReplName:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Run REPL"};
        case InputMode::SetHeadlineTags:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Set headline tags"};
        case InputMode::ShowMassifGraphPath:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Massif output file"};
        case InputMode::StringRectangle:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "String rectangle"};
        case InputMode::VcsCreateBranch:
            return bufferview::TextEntryPrompt{bufferview::PromptCompletion::None, "Create branch"};

        default:
            return std::nullopt; // not a text-entry prompt
    }
}

bufferview::PromptCommit BufferView::CommitTextEntryPrompt(const std::string& input) {
    if (inputMode_ == InputMode::FindFile) {
        const bool isNewFile = !std::filesystem::exists(input);
        try {
            text::Buffer& opened = bufferList_.OpenOrCreateFile(input);
            activeBuffer_.Set(opened);
            statusMessage_ = isNewFile ? "(New file)" : ("Opened " + opened.Name());
        }
        catch (const text::BinaryFileError&) {
            // Ask whether to open it anyway rather than just reporting the
            // refusal. That is a second y/n prompt taking over, so this reports
            // a hand-off: the session must not end and nothing is recorded.
            prompt_.reset();
            BeginConfirmOpenBinary(input);
            return bufferview::PromptCommit::Transitioned;
        }
        catch (const std::exception& e) {
            ReportError(e.what());
        }
    }
    else if (inputMode_ == InputMode::OpenProjectPath) {
        const std::filesystem::path root = editor::DetectProjectRoot(input);
        if (editor::FindProjectByRoot(root)) {
            // Already registered/named -- nothing to ask, activate directly.
            ActivateProjectAndReport(root);
        }
        else {
            // BookmarkSet's own pre-filled-second-prompt shape --
            // returns rather than falling through to this function's
            // shared EndInteractiveSession() tail below, since this
            // transitions to OpenProjectName instead of finishing.
            pendingOpenProjectRoot_ = root;
            inputMode_              = InputMode::OpenProjectName;
            prompt_.emplace("Project name: ");
            prompt_->SetText(root.filename().string());
            statusMessage_ = prompt_->StatusText();
            return bufferview::PromptCommit::Transitioned;
        }
    }
    else if (inputMode_ == InputMode::OpenProjectName) {
        const std::string           name = input.empty() ? pendingOpenProjectRoot_.filename().string() : input;
        const std::filesystem::path root = pendingOpenProjectRoot_;
        pendingOpenProjectRoot_.clear();
        editor::RegisterProject(name, root);
        ActivateProjectAndReport(root);
    }
    else if (inputMode_ == InputMode::CreateDirectory) {
        try {
            editor::CreateProjectDirectory(input);
            statusMessage_ = "Created directory " + input;
            if (projectSidebar_) {
                projectSidebar_->InvalidateTree();
            }
        }
        catch (const std::exception& e) {
            ReportError(e.what());
        }
    }
    else if (inputMode_ == InputMode::ProjectSearch) {
        try {
            const std::vector<editor::SearchMatch> matches =
                editor::SearchDirectory(editor::ProjectRoot(), input);

            if (matches.empty()) {
                statusMessage_ = "No matches for \"" + input + "\"";
            }
            else {
                BuildResultsBuffer(matches, "*search results*");
                statusMessage_ = std::to_string(matches.size()) + " match" + (matches.size() == 1 ? "" : "es") +
                                 " for \"" + input + "\" -- C-c C-v to visit";
            }
        }
        catch (const editor::SearchPatternError& e) {
            ReportError(std::string("Invalid regex: ") + e.what());
        }
    }
    else if (inputMode_ == InputMode::StringRectangle) {
        text::Buffer& buffer = activeBuffer_.Get();
        if (!buffer.HasSecondaryCursors()) {
            editor::StringRectangle(buffer, input, editor::TabWidth());
        }
        else {
            // multi-cursor-round-2 follow-up: one shared, user-typed
            // replacement string applied to every cursor's own
            // rectangle -- no piece-distribution question here, unlike
            // kill/yank.
            buffer.ForEachCursor([&] {
                if (buffer.HasMark()) {
                    editor::StringRectangle(buffer, input, editor::TabWidth());
                }
            });
        }
        statusMessage_.clear();
    }
    else if (inputMode_ == InputMode::SetHeadlineTags) {
        // Re-resolved fresh (matches StartInteractiveSession's own
        // comment on why this is safe) rather than trusting a value
        // captured when the prompt opened -- point hasn't moved, so
        // this always finds the same headline.
        if (const auto headline = editor::org::HeadlineAtPoint(activeBuffer_.Get())) {
            std::vector<std::string> newTags;
            std::string              current;
            for (const char ch : input) {
                if (ch == ':') {
                    if (!current.empty()) {
                        newTags.push_back(current);
                        current.clear();
                    }
                }
                else {
                    current.push_back(ch);
                }
            }
            if (!current.empty())
                newTags.push_back(current);
            editor::org::SetHeadlineTags(activeBuffer_.Get(), *headline, newTags);
        }
        statusMessage_.clear();
    }
    else if (inputMode_ == InputMode::DeleteProperty) {
        if (editor::org::DeletePropertyAtPoint(activeBuffer_.Get(), input)) {
            statusMessage_.clear();
        }
        else {
            statusMessage_ = "No such property.";
        }
    }
    else if (inputMode_ == InputMode::OrgSchedule || inputMode_ == InputMode::OrgDeadline) {
        // Re-resolved fresh, same reasoning SetHeadlineTags's own branch
        // above states.
        const bool isDeadline = (inputMode_ == InputMode::OrgDeadline);
        if (const auto headline = editor::org::HeadlineAtPoint(activeBuffer_.Get())) {
            editor::org::Planning planning =
                editor::org::ParsePlanning(activeBuffer_.Get().Text(), *headline).value_or(editor::org::Planning{});
            if (input.empty()) {
                // Empty input clears this slot -- same "empty removes
                // it" precedent SetHeadlineTags's own empty-tags case
                // and SetProperty's own empty-value case establish.
                (isDeadline ? planning.deadline : planning.scheduled) = std::nullopt;
                editor::org::SetPlanning(activeBuffer_.Get(), *headline, planning);
                statusMessage_.clear();
            }
            else {
                const auto today = std::chrono::year_month_day{
                    std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
                if (const auto parsed = editor::org::ParseTimestampInput(input, today)) {
                    (isDeadline ? planning.deadline : planning.scheduled) = parsed;
                    editor::org::SetPlanning(activeBuffer_.Get(), *headline, planning);
                    statusMessage_.clear();
                }
                else {
                    statusMessage_ = "Unrecognized date -- try \"today\", \"+N\", or \"YYYY-MM-DD[ HH:MM]\".";
                }
            }
        }
    }
    else if (inputMode_ == InputMode::LspRenameNewName) {
        // Fire-and-forget, same async shape as RequestCodeActionsAtPoint:
        // EndInteractiveSession() below runs immediately, the actual
        // rename applies later, from inside RequestRenameAtPoint's own
        // callback, once the response arrives -- no separate y/n
        // confirmation (worst case, undo).
        RequestRenameAtPoint(input);
    }
    else if (inputMode_ == InputMode::TaskName) {
        if (input.empty()) {
            statusMessage_ = "No task name given.";
        }
        else if (!taskRunner_) {
            statusMessage_ = "No task runner available.";
        }
        else if (taskPromptAction_ == TaskPromptAction::Run) {
            if (text::Buffer* buffer = taskRunner_->RunTask(input)) {
                activeBuffer_.Set(*buffer);
                statusMessage_.clear();
            }
        }
        else { // Cancel
            if (taskRunner_->IsRunning(input)) {
                taskRunner_->CancelTask(input);
                statusMessage_ = "Cancelling task \"" + input + "\"...";
            }
            else {
                statusMessage_ = "No running task named \"" + input + "\"";
            }
        }
    }
    else if (inputMode_ == InputMode::ReplName) {
        if (input.empty()) {
            statusMessage_ = "No REPL name given.";
        }
        else if (!editor::repl::Command(input).has_value()) {
            statusMessage_ = "No REPL command configured for \"" + input + "\" (see ned/set-repl-command).";
        }
        else if (!onRunReplRequest_) {
            statusMessage_ = "No REPL host available.";
        }
        else {
            onRunReplRequest_(input);
            statusMessage_.clear();
        }
    }
    else if (inputMode_ == InputMode::AcpPromptText) {
        // Fire-and-forget, same async shape as DapEvaluate below: the
        // reply streams into the output buffer asynchronously via
        // AcpManager's own session/update handling, not through this
        // return value.
        statusMessage_ = acpManager_ ? acpManager_->SendPrompt(input) : "No ACP manager available.";
    }
    else if (inputMode_ == InputMode::DapEvaluate) {
        if (input.empty()) {
            statusMessage_.clear();
        }
        else if (!dapManager_) {
            statusMessage_ = "No debugger available.";
        }
        else {
            // Fire-and-forget, same async shape as LspRenameNewName
            // above: EndInteractiveSession() below runs immediately,
            // the result lands in statusMessage_ from the callback.
            statusMessage_ = "Evaluating...";
            dapManager_->Evaluate(input, [this, input](bool success, std::string text) {
                statusMessage_ = success ? (input + " = " + text) : ("Evaluate failed: " + text);
            });
        }
    }
    else if (inputMode_ == InputMode::DapBreakpointCondition || inputMode_ == InputMode::DapBreakpointLogMessage ||
             inputMode_ == InputMode::DapBreakpointHitCondition) {
        // Empty input is meaningful here (clears the field), unlike
        // DapEvaluate above -- no early-return on it.
        if (!dapManager_ || !pendingDapBreakpointTarget_) {
            statusMessage_ = "No debugger available.";
        }
        else if (inputMode_ == InputMode::DapBreakpointCondition) {
            statusMessage_ = dapManager_->SetBreakpointCondition(pendingDapBreakpointTarget_->path, pendingDapBreakpointTarget_->line, input);
        }
        else if (inputMode_ == InputMode::DapBreakpointLogMessage) {
            statusMessage_ = dapManager_->SetBreakpointLogMessage(pendingDapBreakpointTarget_->path, pendingDapBreakpointTarget_->line, input);
        }
        else {
            statusMessage_ = dapManager_->SetBreakpointHitCondition(pendingDapBreakpointTarget_->path, pendingDapBreakpointTarget_->line, input);
        }
        pendingDapBreakpointTarget_.reset();
    }
    else if (inputMode_ == InputMode::DapFunctionBreakpointName) {
        if (input.empty()) {
            statusMessage_ = "No function name given.";
        }
        else if (!dapManager_) {
            statusMessage_ = "No debugger available.";
        }
        else {
            const bool nowSet = dapManager_->ToggleFunctionBreakpoint(input);
            statusMessage_    = (nowSet ? "Function breakpoint added: " : "Function breakpoint removed: ") + input;
        }
    }
    else if (inputMode_ == InputMode::DapAddWatch) {
        if (input.empty()) {
            statusMessage_ = "No expression given.";
        }
        else if (!dapManager_) {
            statusMessage_ = "No debugger available.";
        }
        else {
            dapManager_->AddWatch(input);
            statusMessage_ = "Watch added: " + input;
        }
    }
    else if (inputMode_ == InputMode::DapSetVariableValue) {
        if (!dapManager_ || !pendingDapSetVariable_) {
            statusMessage_ = "No debugger available.";
        }
        else {
            text::Buffer* const bufferPtr = pendingDapSetVariable_->buffer;
            const std::size_t   line      = pendingDapSetVariable_->line;
            const std::string   lineText  = pendingDapSetVariable_->lineText;
            const int           ownerRef  = pendingDapSetVariable_->ownerRef;
            const std::string   name      = pendingDapSetVariable_->name;
            statusMessage_                = "Setting " + name + "...";
            dapManager_->SetVariable(
                ownerRef, name, input,
                [this, bufferPtr, line, lineText, name](editor::dap::Manager::SetVariableResult result) {
                    if (bufferPtr != &activeBuffer_.Get()) {
                        return; // switched away while the request was in flight
                    }
                    if (!result.success) {
                        statusMessage_ = "Set variable failed: " + result.errorMessage;
                        return;
                    }
                    text::Buffer&             target        = *bufferPtr;
                    const text::ITextStorage& targetContent = target.Content();
                    if (line >= targetContent.LineCount()) {
                        return;
                    }
                    const std::size_t targetLineStart = targetContent.LineToByteOffset(line);
                    const std::size_t targetLineEnd   = (line + 1 < targetContent.LineCount())
                                                            ? targetContent.LineToByteOffset(line + 1) - 1
                                                            : targetContent.ByteLength();
                    if (targetContent.Substring(targetLineStart, targetLineEnd - targetLineStart) != lineText) {
                        statusMessage_ = "Debug line changed -- variable set on the adapter, but not re-displayed.";
                        return;
                    }
                    // Rebuild this single line the same way FormatDebugVariableLine
                    // would, preserving indent and the owner marker (whose ref
                    // hasn't changed) but reflecting the possibly-new value/type/
                    // ref -- same programmatic-splice-under-a-lifted-read-only-flag
                    // pattern ExpandVariableAtPoint uses.
                    std::size_t indent = 0;
                    while (indent < lineText.size() && lineText[indent] == ' ') {
                        ++indent;
                    }
                    editor::dap::Manager::Variable variable;
                    variable.name                 = name;
                    variable.value                = result.value;
                    variable.type                 = result.type;
                    variable.variablesReference   = result.variablesReference;
                    std::string       replacement = FormatDebugVariableLine(variable, indent);
                    const std::size_t ownerMarker = lineText.rfind("[owner:");
                    if (ownerMarker != std::string::npos) {
                        replacement += "  " + lineText.substr(ownerMarker);
                    }
                    const bool wasReadOnly = target.ReadOnly();
                    target.SetReadOnly(false);
                    target.DeleteRange(targetLineStart, targetLineEnd - targetLineStart);
                    target.InsertAt(targetLineStart, replacement);
                    target.SetReadOnly(wasReadOnly);
                    statusMessage_.clear();
                });
        }
        pendingDapSetVariable_.reset();
    }
    else if (inputMode_ == InputMode::DapMemoryByteCount) {
        if (!dapManager_ || !pendingDapMemoryReference_) {
            statusMessage_ = "No debugger available.";
        }
        else {
            std::size_t count = 128; // DAP round 5's default -- enough for most pointer/array previews
            if (!input.empty()) {
                try {
                    const long parsed = std::stol(input);
                    if (parsed > 0) {
                        count = static_cast<std::size_t>(parsed);
                    }
                }
                catch (const std::exception&) {
                    // Keep the default -- an unparsable count isn't worth failing the request over.
                }
            }
            const std::string memoryReference = *pendingDapMemoryReference_;
            const bool        asImage         = pendingDapMemoryAsImage_;
            statusMessage_                    = "Fetching memory...";
            dapManager_->RequestMemory(
                memoryReference, 0, count,
                [this, memoryReference, asImage](bool success, editor::dap::Manager::MemoryBlock block) {
                    if (!success) {
                        statusMessage_ = "Read memory failed (adapter may not support readMemory).";
                        return;
                    }
                    if (asImage) {
                        PushMemoryImageModel(memoryReference, block);
                    }
                    else {
                        BuildMemoryBuffer(memoryReference, block);
                    }
                });
        }
        pendingDapMemoryReference_.reset();
        pendingDapMemoryAsImage_ = false;
    }
    else if (inputMode_ == InputMode::ShowMassifGraphPath) {
        if (input.empty()) {
            statusMessage_ = "No massif output file given.";
        }
        else {
            std::ifstream file(input, std::ios::binary);
            if (!file) {
                ReportError("Cannot open " + input);
            }
            else {
                std::ostringstream contents;
                contents << file.rdbuf();
                const editor::MassifProfile profile = editor::ParseMassifOutput(contents.str());
                if (profile.snapshots.empty()) {
                    statusMessage_ = "No massif snapshots found in " + input;
                }
                else {
                    text::Buffer& report = editor::RebuildMassifReportBuffer(bufferList_, profile, input);
                    activeBuffer_.Set(report);
                    statusMessage_ = "Massif report: " + std::to_string(profile.snapshots.size()) + " snapshots";
                }
            }
        }
    }
    else if (inputMode_ == InputMode::VcsCreateBranch) {
        if (input.empty()) {
            statusMessage_ = "No branch name given.";
        }
        else if (!vcsRunner_) {
            statusMessage_ = "no vcs runner configured";
        }
        else {
            statusMessage_ = "Creating branch " + input + "...";
            // A branch switch rewrites the working tree underneath any
            // open buffer. Unmodified buffers catch up on the next
            // auto-revert tick (external-modification-safety follow-up,
            // Editor/AutoRevert.h -- this message predates it and used
            // to say "not reloaded"); a *modified* buffer is still left
            // alone, and its save will hit the supersession y/n rather
            // than a confusing stale-content overwrite.
            vcsRunner_->RequestBranchCreate(
                input,
                [this, input] {
                    statusMessage_ = "Created and switched to " + input + " (modified buffers not reloaded)";
                    RefreshVcsStatusBuffer();
                    RequestDiffForCurrentBuffer();
                },
                [this](std::string error) { statusMessage_ = "vcs branch: " + error; });
        }
    }
    else if (inputMode_ == InputMode::GotoLine) {
        std::size_t parsed   = 0;
        bool        allDigit = !input.empty();
        for (const char ch : input) {
            if (ch < '0' || ch > '9') {
                allDigit = false;
                break;
            }
            parsed = parsed * 10 + static_cast<std::size_t>(ch - '0');
        }
        if (!allDigit) {
            statusMessage_ = "Not a line number: \"" + input + "\"";
        }
        else {
            // 1-based like Emacs' own goto-line; out-of-range clamps to
            // the last line rather than erroring.
            text::Buffer&     buffer = activeBuffer_.Get();
            const std::size_t target = std::min(std::max<std::size_t>(parsed, 1), buffer.Content().LineCount()) - 1;
            PushJumpMark();
            buffer.SetPoint(buffer.Content().LineToByteOffset(target));
            statusMessage_.clear();
        }
    }
    else if (inputMode_ == InputMode::BookmarkSetName) {
        if (input.empty()) {
            statusMessage_ = "Bookmark name cannot be empty";
        }
        else {
            editor::RecordBookmark(input, activeBuffer_.Get(), static_cast<std::size_t>(editor::TabWidth()));
            editor::SaveBookmarks();
            statusMessage_ = "Bookmark set: " + input;
        }
    }
    else { // FindScratch
        if (!editor::IsValidScratchName(input)) {
            statusMessage_ = "Invalid scratch name: \"" + input + "\"";
        }
        else {
            try {
                std::filesystem::create_directories(editor::ScratchDirectory());
                text::Buffer& opened = bufferList_.OpenOrCreateFile(editor::ScratchPathForName(input));
                activeBuffer_.Set(opened);
                statusMessage_ = "Scratch: " + input;
            }
            catch (const std::exception& e) {
                ReportError(e.what());
            }
        }
    }

    return bufferview::PromptCommit::Finished;
}

void BufferView::HandlePromptKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        const std::string input = prompt_->Text();
        if (CommitTextEntryPrompt(input) == bufferview::PromptCommit::Transitioned) {
            return; // handed off to a second prompt, which owns the session now
        }
        promptHistory_.Record(HistoryKeyForInputMode(inputMode_), input);
        EndInteractiveSession();
        return;
    }
    if (IsQuit(chord)) {
        // status-message-lifecycle follow-up: a real, visible "cancelled"
        // message rather than an immediate blank -- the new auto-clear
        // mechanism (EnsureStatusMessageFreshness/OnAnimation) takes it
        // from here, the same way it does for any other status message.
        const std::optional<bufferview::TextEntryPrompt> prompt = TextEntryPromptFor(inputMode_);
        statusMessage_                                          = (prompt ? prompt->cancelLabel : std::string("Prompt")) + " cancelled.";
        EndInteractiveSession();
        return;
    }
    if (PromptCompletionForCurrentMode() == bufferview::PromptCompletion::PathDropdown) {
        // dropdown-path-completion follow-up: Up/Down move the live popup's
        // highlight (prompt history stays on M-p/M-n, TryNavigatePromptHistory
        // below -- never plain Up/Down, so there's no conflict); Tab accepts
        // the highlighted candidate's full accumulated value (not the masked
        // display text) into the prompt rather than the old common-prefix-
        // expand-and-list-in-the-echo-area behavior -- a directory candidate's
        // trailing '/' means the very next refresh re-lists that directory's
        // own contents, giving a descend-by-Tab feel. Enter/Quit are handled
        // by the generic chains above/below, unchanged -- this mode still
        // finalizes on literal prompt_->Text(), not the popup selection.
        if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
            const std::vector<std::string> candidates = GatherPathCompletionCandidates();
            if (!candidates.empty()) {
                pathCompletionSelection_ = chord.Special == editor::SpecialKey::Down
                                               ? (pathCompletionSelection_ + 1) % candidates.size()
                                               : (pathCompletionSelection_ + candidates.size() - 1) % candidates.size();
            }
            RefreshPathCompletionPopup();
            return;
        }
        if (chord.Special == editor::SpecialKey::Tab) {
            const std::vector<std::string> candidates = GatherPathCompletionCandidates();
            if (!candidates.empty()) {
                prompt_->SetText(candidates[std::min(pathCompletionSelection_, candidates.size() - 1)]);
                pathCompletionSelection_ = 0;
            }
            RefreshPathCompletionPopup();
            return;
        }
    }
    if (chord.Special == editor::SpecialKey::Tab &&
        PromptCompletionForCurrentMode() == bufferview::PromptCompletion::Names) {
        CompletePrompt();
        return;
    }

    if (TryNavigatePromptHistory(chord, HistoryKeyForInputMode(inputMode_))) {
        statusMessage_ = prompt_->StatusText();
        return;
    }

    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        promptHistoryIndex_ = kNoHistoryIndex; // editing exits history browsing -- see TryNavigatePromptHistory's own doc comment
        if (PromptCompletionForCurrentMode() == bufferview::PromptCompletion::PathDropdown) {
            pathCompletionSelection_ = 0;
            RefreshPathCompletionPopup();
            return;
        }
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.

    statusMessage_ = prompt_->StatusText();
}

BufferView::PromptEditOutcome BufferView::HandlePromptEditingKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Backspace) {
        prompt_->DeleteBackward();
        return PromptEditOutcome::TextEdited;
    }
    if (chord.Special == editor::SpecialKey::Delete) {
        prompt_->DeleteForward();
        return PromptEditOutcome::TextEdited;
    }
    if (chord.Special == editor::SpecialKey::Left) {
        prompt_->MoveCursorLeft();
        return PromptEditOutcome::CursorMoved;
    }
    if (chord.Special == editor::SpecialKey::Right) {
        prompt_->MoveCursorRight();
        return PromptEditOutcome::CursorMoved;
    }
    if (chord.Special == editor::SpecialKey::Home) {
        prompt_->MoveCursorToStart();
        return PromptEditOutcome::CursorMoved;
    }
    if (chord.Special == editor::SpecialKey::End) {
        prompt_->MoveCursorToEnd();
        return PromptEditOutcome::CursorMoved;
    }
    if (IsPlainCharacter(chord)) {
        prompt_->InsertChar(chord.Codepoint);
        return PromptEditOutcome::TextEdited;
    }
    return PromptEditOutcome::NotHandled;
}

// dropdown-path-completion follow-up: the candidate source
// RefreshPathCompletionPopup/the Up-Down-Tab handling in HandlePromptKey
// below all share -- FindFile/OpenProjectPath/FindScratch's own directory-
// or name-prefix-filtered lists, unchanged from what CompletePrompt used to
// gather inline before Tab was the only trigger.

void BufferView::CompletePrompt() {
    // dropdown-path-completion follow-up: FindFile/OpenProjectPath/FindScratch
    // (path/name modes that still need to finalize on literal typed text --
    // see RefreshPathCompletionPopup's own doc comment) now get a live popup
    // instead of Tab reaching this function; VcsSwitchBranch/AcpAgentName/
    // SwitchToBuffer are fully dedicated Handle<Mode>Key sessions, intercepted
    // before this function is ever reached. Everything left below is the
    // original catch-all default, still used by whichever other prompt modes
    // reach Tab without their own candidate source (e.g. OpenProjectName,
    // BookmarkSetName).
    std::vector<std::string> candidates = text::CompleteBufferNames(bufferList_, prompt_->Text());

    if (candidates.empty()) {
        statusMessage_ = prompt_->StatusText();
        return;
    }

    const std::string commonPrefix = LongestCommonPrefix(candidates);
    if (commonPrefix.size() > prompt_->Text().size()) {
        prompt_->SetText(commonPrefix);
    }

    statusMessage_ = (candidates.size() == 1) ? prompt_->StatusText()
                                              : prompt_->StatusText() + "  {" + JoinCandidates(candidates) + "}";
}

void BufferView::HandleProjectReplaceKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        projectReplace_->Cancel();
        statusMessage_ = "Project replace cancelled.";
        EndInteractiveSession();
        return;
    }

    const auto stage = projectReplace_->CurrentStage();

    if (stage == editor::ProjectReplace::Stage::EnteringPattern ||
        stage == editor::ProjectReplace::Stage::EnteringReplacement) {
        if (chord.Special == editor::SpecialKey::Enter) {
            if (stage == editor::ProjectReplace::Stage::EnteringPattern) {
                try {
                    projectReplace_->ConfirmPattern();
                }
                catch (const editor::SearchPatternError& e) {
                    ReportError(std::string("Invalid regex: ") + e.what());
                    return; // stays in EnteringPattern; don't overwrite the message below
                }
                // Shown as soon as the pattern is confirmed -- while the
                // replacement text is still being typed and while the final
                // y/n confirmation is pending, not just a terse count once
                // everything's already decided. See ROADMAP.md.
                if (!projectReplace_->Matches().empty()) {
                    BuildResultsBuffer(projectReplace_->Matches(), "*project replace*");
                }
            }
            else {
                projectReplace_->ConfirmReplacement();
            }
        }
        else if (chord.Special == editor::SpecialKey::Backspace) {
            projectReplace_->DeleteChar();
        }
        else if (IsPlainCharacter(chord)) {
            projectReplace_->AppendChar(chord.Codepoint);
        }

        statusMessage_ = projectReplace_->StatusText();

        if (projectReplace_->CurrentStage() == editor::ProjectReplace::Stage::Done) {
            EndInteractiveSession(); // ConfirmReplacement found no matches -- StatusText() already said so
        }
        return;
    }

    // Confirming: a single whole-batch y/n, not QueryReplace's per-match y/n/!/q.
    if (chord.Codepoint == U'y') {
        // in-file-regex follow-up: ConfirmPattern validated this same
        // pattern text against RE2 (SearchDirectory's engine) and the
        // rewrite now runs on PCRE2 (RegexPattern), which accepts
        // essentially everything RE2 does -- but the rewrite can still
        // throw at match time if the match-limit safety net trips (see
        // RegexPattern.h). Caught here rather than left to propagate, same
        // as every other interactive failure in this file.
        try {
            const editor::ReplaceSummary summary = projectReplace_->Confirm();
            statusMessage_                       = "Replaced " + std::to_string(summary.replacementCount) + " occurrence" +
                                                   (summary.replacementCount == 1 ? "" : "s") + " in " + std::to_string(summary.filesChanged) +
                                                   " file" + (summary.filesChanged == 1 ? "" : "s") + ".";
        }
        catch (const std::exception& e) {
            ReportError(std::string("Project replace: ") + e.what());
        }
        EndInteractiveSession();
    }
    else if (chord.Codepoint == U'n') {
        projectReplace_->Cancel();
        statusMessage_ = "Project replace cancelled.";
        EndInteractiveSession();
    }
    // Anything else is ignored -- stay in Confirming.
}

void BufferView::HandleChoicePromptKey(const bufferview::ChoicePrompt& prompt, const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_ = prompt.cancelMessage;
        EndInteractiveSession();
        return;
    }
    if (prompt.count == 0) {
        return;
    }
    std::size_t& selection = *prompt.selection;

    if (chord.Special == editor::SpecialKey::Down) {
        selection = (selection + 1) % prompt.count;
        prompt.refresh();
        return;
    }
    if (chord.Special == editor::SpecialKey::Up) {
        selection = (selection + prompt.count - 1) % prompt.count;
        prompt.refresh();
        return;
    }
    if (IsPlainCharacter(chord) && chord.Codepoint >= U'1' && chord.Codepoint <= U'9') {
        const std::size_t index = static_cast<std::size_t>(chord.Codepoint - U'1');
        if (index >= prompt.count) {
            return; // no entry behind that digit -- stay, rather than committing the highlighted one
        }
        selection = index;
    }
    else if (chord.Special != editor::SpecialKey::Enter) {
        return; // anything else is ignored -- stay in the list
    }

    const std::size_t chosen = selection;
    EndInteractiveSession();
    prompt.commit(chosen);
}

void BufferView::HandleConfirmPromptKey(const bufferview::ConfirmPrompt& prompt, const editor::KeyChord& chord) {
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        EndInteractiveSession();
        prompt.onConfirm();
        return;
    }
    if (chord.Codepoint == U'n' || chord.Codepoint == U'N' || IsQuit(chord)) {
        statusMessage_ = prompt.cancelMessage;
        EndInteractiveSession();
        return;
    }
    // Anything else is ignored: a stray keystroke must not answer a question
    // about discarding work.
}

void BufferView::ForceSaveBuffer() {
    editor::CommandContext context = MakeContext();
    try {
        dispatcher_.Registry().Invoke("save-buffer-force", context);
    }
    catch (const std::exception& e) {
        ReportError(e.what());
    }
}

bufferview::ConfirmPrompt BufferView::ConfirmDeleteFilePrompt() {
    // Captured now: ending the session clears deleteTarget_.
    return {.cancelMessage = "Delete cancelled.", .onConfirm = [this, target = deleteTarget_] {
                try {
                    editor::DeleteProjectPath(target);
                    statusMessage_ = "Deleted " + target.string();
                    if (projectSidebar_) {
                        projectSidebar_->InvalidateTree();
                    }
                }
                catch (const std::exception& e) {
                    ReportError(e.what());
                }
            }};
}

bufferview::ConfirmPrompt BufferView::ConfirmRecoverFilePrompt() {
    // The chosen version is captured by value rather than re-read through
    // recoverVersions_[recoverChoice_], which belongs to the session.
    const editor::BackupVersion chosen =
        recoverChoice_ < recoverVersions_.size() ? recoverVersions_[recoverChoice_] : editor::BackupVersion{};
    return {.cancelMessage = "Recover cancelled.", .onConfirm = [this, chosen] {
                try {
                    const std::string content = editor::ReadBackupVersion(chosen.path);
                    activeBuffer_.Get().RestoreContent(content);
                    statusMessage_ = "Recovered " + chosen.label + " -- buffer is modified; save to keep it";
                }
                catch (const std::exception& e) {
                    ReportError(e.what());
                }
            }};
}

bufferview::ConfirmPrompt BufferView::ConfirmQuitPrompt() {
    return {.cancelMessage = "Quit cancelled.", .onConfirm = [this] {
                statusMessage_ = "Shutting down...";
                if (eventLoop_) {
                    eventLoop_->Exit();
                }
            }};
}

bufferview::ConfirmPrompt BufferView::ConfirmCloseBufferPrompt() {
    // Captured now: ending the session clears pendingClose_, and it must be
    // clear before CloseBufferNow touches the active buffer.
    return {.cancelMessage = "Close cancelled.", .onConfirm = [this, buffer = pendingClose_] {
                if (buffer != nullptr) {
                    CloseBufferNow(*buffer);
                }
            }};
}

bufferview::ConfirmPrompt BufferView::ConfirmOverwriteSavePrompt() {
    return {.cancelMessage = "Save cancelled; the file on disk was left as-is.",
            .onConfirm     = [this] { ForceSaveBuffer(); }};
}

bufferview::ConfirmPrompt BufferView::ConfirmSaveWithConflictsPrompt() {
    return {.cancelMessage = "Save cancelled; resolve the <<<<<<< markers first.",
            .onConfirm     = [this] { ForceSaveBuffer(); }};
}

bufferview::ConfirmPrompt BufferView::ConfirmOpenBinaryPrompt() {
    // Captured for the same reason ConfirmCloseBufferPrompt captures its buffer.
    return {.cancelMessage = "Open cancelled.", .onConfirm = [this, path = pendingBinaryOpenPath_] {
                try {
                    text::Buffer& opened = bufferList_.OpenOrCreateFile(path, /*allowBinary=*/true);
                    activeBuffer_.Set(opened);
                    statusMessage_ = "Opened " + opened.Name();
                }
                catch (const std::exception& e) {
                    ReportError(e.what());
                }
            }};
}

void BufferView::HandleConfirmQuitKey(const editor::KeyChord& chord) {
    HandleConfirmPromptKey(ConfirmQuitPrompt(), chord);
}

void BufferView::HandleConfirmCloseBufferKey(const editor::KeyChord& chord) {
    HandleConfirmPromptKey(ConfirmCloseBufferPrompt(), chord);
}

void BufferView::HandleConfirmOverwriteSaveKey(const editor::KeyChord& chord) {
    HandleConfirmPromptKey(ConfirmOverwriteSavePrompt(), chord);
}

void BufferView::HandleConfirmSaveWithConflictsKey(const editor::KeyChord& chord) {
    HandleConfirmPromptKey(ConfirmSaveWithConflictsPrompt(), chord);
}

void BufferView::RequestOpenBinaryFile(const std::filesystem::path& path) {
    if (inputMode_ != InputMode::Normal) {
        statusMessage_ = "Finish the current prompt first.";
        return;
    }
    BeginConfirmOpenBinary(path);
}

void BufferView::HandleConfirmOpenBinaryKey(const editor::KeyChord& chord) {
    HandleConfirmPromptKey(ConfirmOpenBinaryPrompt(), chord);
}

void BufferView::RequestTrustProjectInit(
    const std::filesystem::path&                                                   initPath,
    std::function<void(const std::filesystem::path&, editor::ProjectInitDecision)> onDecision) {
    if (inputMode_ != InputMode::Normal) {
        statusMessage_ = "Finish the current prompt first.";
        return;
    }
    pendingTrustInitPath_ = initPath;
    onTrustDecision_      = std::move(onDecision);
    inputMode_            = InputMode::ConfirmTrustProjectInit;
    statusMessage_        = "Load project init \"" + initPath.string() + "\"? (y=once, a=always, n=no)";
}

void BufferView::HandleConfirmTrustProjectInitKey(const editor::KeyChord& chord) {
    std::optional<editor::ProjectInitDecision> decision;
    if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
        decision = editor::ProjectInitDecision::LoadOnce;
    }
    else if (chord.Codepoint == U'a' || chord.Codepoint == U'A') {
        decision = editor::ProjectInitDecision::LoadAlways;
    }
    else if (chord.Codepoint == U'n' || chord.Codepoint == U'N' || IsQuit(chord)) {
        decision = editor::ProjectInitDecision::Decline;
    }
    if (!decision) {
        return; // anything else is ignored -- stay in the prompt
    }

    // Moved out before EndInteractiveSession clears both members; the
    // callback runs after the session has fully ended so it's free to set
    // statusMessage_ (and could even start a new prompt) without this
    // session's teardown clobbering it.
    const std::filesystem::path path       = pendingTrustInitPath_;
    const auto                  onDecision = std::move(onTrustDecision_);
    EndInteractiveSession();
    if (onDecision) {
        onDecision(path, *decision);
    }
}

void BufferView::StartCreateFileAt(const std::filesystem::path& directory) {
    if (inputMode_ != InputMode::Normal) {
        statusMessage_ = "Finish the current prompt first.";
        return;
    }
    std::string prefill = directory.string();
    if (!prefill.empty() && prefill.back() != '/') {
        prefill += '/';
    }
    inputMode_ = InputMode::FindFile;
    prompt_.emplace("Find file: ");
    prompt_->SetText(prefill);
    pathCompletionSelection_ = 0;
    RefreshPathCompletionPopup();
}

void BufferView::StartCreateDirectoryAt(const std::filesystem::path& directory) {
    if (inputMode_ != InputMode::Normal) {
        statusMessage_ = "Finish the current prompt first.";
        return;
    }
    std::string prefill = directory.string();
    if (!prefill.empty() && prefill.back() != '/') {
        prefill += '/';
    }
    inputMode_ = InputMode::CreateDirectory;
    prompt_.emplace("Create directory: ");
    prompt_->SetText(prefill);
    statusMessage_ = prompt_->StatusText();
}

void BufferView::StartDeleteFileAt(const std::filesystem::path& path) {
    if (inputMode_ != InputMode::Normal) {
        statusMessage_ = "Finish the current prompt first.";
        return;
    }
    if (!std::filesystem::exists(path)) {
        statusMessage_ = "No such file or directory: " + path.string();
        return;
    }
    inputMode_     = InputMode::DeleteFile;
    deleteStage_   = DeleteFileStage::Confirming;
    deleteTarget_  = path;
    statusMessage_ = "Delete \"" + path.string() + "\"? (y/n)";
}

void BufferView::HandleDeleteFileKey(const editor::KeyChord& chord) {
    if (deleteStage_ == DeleteFileStage::EnteringPath) {
        if (chord.Special == editor::SpecialKey::Enter) {
            const std::string input = prompt_->Text();
            if (!std::filesystem::exists(input)) {
                statusMessage_ = "No such file or directory: " + input;
                EndInteractiveSession();
                return;
            }
            deleteTarget_ = input;
            deleteStage_  = DeleteFileStage::Confirming;
            prompt_.reset();
            statusMessage_ = "Delete \"" + deleteTarget_.string() + "\"? (y/n)";
            return;
        }
        if (IsQuit(chord)) {
            statusMessage_ = "Delete cancelled.";
            EndInteractiveSession();
            return;
        }
        (void)HandlePromptEditingKey(chord); // outcome doesn't matter here -- always just re-echoes the prompt text
        statusMessage_ = prompt_->StatusText();
        return;
    }

    // Confirming: an ordinary y/n confirmation now the target is chosen.
    HandleConfirmPromptKey(ConfirmDeleteFilePrompt(), chord);
}

void BufferView::HandleSetPropertyKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        const std::string input = prompt_->Text();

        if (propertyStage_ == PropertyPromptStage::EnteringName) {
            if (input.empty()) {
                statusMessage_ = "Property name cannot be empty.";
                EndInteractiveSession();
                return;
            }
            pendingPropertyName_ = input;
            propertyStage_       = PropertyPromptStage::EnteringValue;
            prompt_.emplace("Value for :" + pendingPropertyName_ + ": ");
            // Pre-fill with the property's current value, if it already has
            // one -- same "prompt opens pre-filled with what's already
            // there" precedent SetHeadlineTags's own StartInteractiveSession
            // case establishes.
            if (const auto headline = editor::org::HeadlineAtPoint(activeBuffer_.Get())) {
                if (const auto current = editor::org::GetProperty(activeBuffer_.Get().Text(), *headline, pendingPropertyName_)) {
                    prompt_->SetText(*current);
                }
            }
            statusMessage_ = prompt_->StatusText();
            return;
        }

        // EnteringValue -- re-resolves HeadlineAtPoint fresh rather than
        // trusting anything captured when the session opened, same
        // reasoning SetHeadlineTags's own doc comment states (point can't
        // have moved since org-set-property's own precondition check).
        if (editor::org::SetPropertyAtPoint(activeBuffer_.Get(), pendingPropertyName_, input)) {
            statusMessage_.clear();
        }
        else {
            statusMessage_ = "Not on a headline.";
        }
        EndInteractiveSession();
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Set property cancelled.";
        EndInteractiveSession();
        return;
    }
    (void)HandlePromptEditingKey(chord);
    statusMessage_ = prompt_->StatusText();
}

void BufferView::HandleRecoverFileKey(const editor::KeyChord& chord) {
    if (recoverStage_ == RecoverFileStage::PickingVersion) {
        if (chord.Special == editor::SpecialKey::Enter) {
            const std::string input  = prompt_->Text();
            std::size_t       choice = 1; // Enter alone means the newest
            if (!input.empty()) {
                try {
                    choice = std::stoul(input);
                }
                catch (const std::exception&) {
                    choice = 0; // non-numeric -- caught by the range check below
                }
            }
            if (choice < 1 || choice > recoverVersions_.size()) {
                statusMessage_ = "No such version: " + input;
                EndInteractiveSession();
                return;
            }
            recoverChoice_ = choice - 1;
            recoverStage_  = RecoverFileStage::Confirming;
            prompt_.reset();
            statusMessage_ = "Recover \"" + recoverVersions_[recoverChoice_].label + "\" over buffer " + activeBuffer_.Get().Name() + "? (y/n)";
            return;
        }
        if (IsQuit(chord)) {
            statusMessage_ = "Recover cancelled.";
            EndInteractiveSession();
            return;
        }
        // Digit-only field -- HandlePromptEditingKey's own plain-character
        // insert would accept anything, so this stays hand-rolled rather
        // than delegating InsertChar the way the free-text prompts do;
        // Backspace/Delete/Left/Right/Home/End all still apply unchanged.
        if (chord.Special == editor::SpecialKey::Backspace) {
            prompt_->DeleteBackward();
        }
        else if (chord.Special == editor::SpecialKey::Delete) {
            prompt_->DeleteForward();
        }
        else if (chord.Special == editor::SpecialKey::Left) {
            prompt_->MoveCursorLeft();
        }
        else if (chord.Special == editor::SpecialKey::Right) {
            prompt_->MoveCursorRight();
        }
        else if (chord.Special == editor::SpecialKey::Home) {
            prompt_->MoveCursorToStart();
        }
        else if (chord.Special == editor::SpecialKey::End) {
            prompt_->MoveCursorToEnd();
        }
        else if (IsPlainCharacter(chord) && chord.Codepoint >= U'0' && chord.Codepoint <= U'9') {
            prompt_->InsertChar(chord.Codepoint);
        }
        statusMessage_ = prompt_->StatusText();
        return;
    }

    // Confirming: an ordinary y/n confirmation now the version is chosen.
    HandleConfirmPromptKey(ConfirmRecoverFilePrompt(), chord);
}

void BufferView::HandleRegisterKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_ = "Register command cancelled.";
        EndInteractiveSession();
        return;
    }
    if (!IsPlainCharacter(chord)) {
        return; // ignore, keep waiting for a register name
    }

    const char32_t name   = chord.Codepoint;
    text::Buffer&  buffer = activeBuffer_.Get();
    switch (inputMode_) {
        case InputMode::PointToRegister: {
            // multi-cursor-round-2 follow-up: [0] is the primary's point,
            // the rest are every secondary's, in cursor order.
            std::vector<std::size_t> points{buffer.Point()};
            for (const auto& cursor : buffer.SecondaryCursors()) {
                points.push_back(cursor.point);
            }
            registers_.SetPoint(name, buffer.Name(), std::move(points));
            statusMessage_ = "Point stored in register.";
            break;
        }
        case InputMode::JumpToRegister:
            if (const editor::PointRegisterValue* value = registers_.Point(name)) {
                if (text::Buffer* target = bufferList_.Find(value->bufferName)) {
                    PushJumpMark();
                    activeBuffer_.Set(*target);
                    // multi-cursor-round-2 follow-up: restores the primary
                    // point plus a secondary cursor for every further saved
                    // offset -- byteOffsets is never empty (RegisterTable::
                    // SetPoint's own guarantee).
                    target->ClearSecondaryCursors();
                    target->SetPoint(value->byteOffsets.front()); // Buffer::SetPoint already clamps out-of-range offsets
                    for (std::size_t i = 1; i < value->byteOffsets.size(); ++i) {
                        target->AddCursorAt(value->byteOffsets[i]);
                    }
                    statusMessage_.clear();
                }
                else {
                    statusMessage_ = "Buffer for that register no longer exists.";
                }
            }
            else {
                statusMessage_ = "Register does not contain a position.";
            }
            break;
        case InputMode::CopyToRegister:
            if (!buffer.HasSecondaryCursors()) {
                if (!buffer.HasMark()) {
                    statusMessage_ = "No region to copy.";
                }
                else {
                    const auto [start, end] = buffer.Region();
                    registers_.SetText(name, buffer.Content().Substring(start, end - start));
                    statusMessage_ = "Copied to register.";
                }
            }
            else {
                // multi-cursor-round-2 follow-up: same "one piece per
                // cursor, empty for a cursor with no mark" shape
                // KillPerCursor uses for kill-region/kill-ring-save.
                std::vector<std::string> pieces;
                bool                     any = false;
                buffer.ForEachCursor([&] {
                    if (buffer.HasMark()) {
                        const auto [start, end] = buffer.Region();
                        pieces.push_back(buffer.Content().Substring(start, end - start));
                        any = true;
                    }
                    else {
                        pieces.emplace_back();
                    }
                });
                if (any) {
                    registers_.SetTextPieces(name, std::move(pieces));
                    statusMessage_ = "Copied to register.";
                }
                else {
                    statusMessage_ = "No region to copy.";
                }
            }
            break;
        case InputMode::InsertRegister: {
            const std::string* blob = registers_.Text(name);
            if (!blob) {
                statusMessage_ = "Register does not contain text.";
                break;
            }
            if (!buffer.HasSecondaryCursors()) {
                buffer.InsertAtPoint(*blob);
                statusMessage_.clear();
                break;
            }
            // multi-cursor-round-2 follow-up: distribute 1:1 in cursor
            // order when the entry's own piece count matches how many
            // cursors are live right now, else fall back to the whole
            // joined blob at every cursor -- same rule multi-cursor yank
            // uses on KillRing.
            const std::vector<std::string>& pieces      = *registers_.TextPieces(name); // guaranteed present alongside blob
            const std::size_t               cursorCount = 1 + buffer.SecondaryCursors().size();
            const bool                      perCursor   = pieces.size() == cursorCount;
            std::size_t                     i           = 0;
            buffer.ForEachCursor([&] {
                buffer.InsertAtPoint(perCursor ? pieces[i] : *blob);
                ++i;
            });
            statusMessage_.clear();
            break;
        }
        default:
            break; // unreachable -- OnKeyEvent only routes here for the four modes above
    }

    EndInteractiveSession();
}

void BufferView::HandleZapToCharKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_ = "Zap to char cancelled.";
        EndInteractiveSession();
        return;
    }
    if (!IsPlainCharacter(chord)) {
        return; // ignore, keep waiting for a target character
    }

    const char32_t target = chord.Codepoint;
    const bool     append = pendingZapToCharAppend_;
    text::Buffer&  buffer = activeBuffer_.Get();

    // The offset just past the next occurrence of `target` at/after `from`,
    // or nullopt if there isn't one -- forward scan, codepoint-granular
    // (matches Buffer's own word-motion scanning style).
    const auto findForward = [&](std::size_t from) -> std::optional<std::size_t> {
        const text::ITextStorage& content = buffer.Content();
        const std::size_t         total   = content.ByteLength();
        std::size_t               offset  = from;
        while (offset < total) {
            const auto decoded = content.CodepointAt(offset);
            offset += decoded.byteLength;
            if (decoded.codepoint == target) {
                return offset;
            }
        }
        return std::nullopt;
    };

    if (!buffer.HasSecondaryCursors()) {
        const std::size_t point = buffer.Point();
        if (const auto end = findForward(point)) {
            buffer.ClearMark();
            std::string text = buffer.DeleteRange(point, *end - point);
            if (append) {
                killRing_.AppendToCurrent(std::move(text), /*prepend=*/false);
            }
            else {
                killRing_.Kill(std::move(text));
            }
            editor::CopyToSystemClipboard(killRing_.Current());
            statusMessage_.clear();
        }
        else {
            statusMessage_ = "No such character.";
        }
        EndInteractiveSession();
        return;
    }

    // multi-cursor: one piece per cursor, empty for a cursor with no match
    // -- KillPerCursor's own resolution (Commands.cpp), duplicated here for
    // the same reason CopyToRegister above duplicates it: KillPerCursor is
    // file-local to Commands.cpp. Multi-cursor kill-append is a deliberate
    // v1 cut (KillRing::AppendToCurrent's own doc comment) -- always a
    // fresh entry here regardless of `append`.
    std::vector<std::string> pieces;
    bool                     any = false;
    buffer.ForEachCursor([&] {
        const std::size_t point = buffer.Point();
        if (const auto end = findForward(point)) {
            buffer.ClearMark();
            pieces.push_back(buffer.DeleteRange(point, *end - point));
            any = true;
        }
        else {
            pieces.emplace_back();
        }
    });
    if (any) {
        killRing_.KillPieces(std::move(pieces));
        editor::CopyToSystemClipboard(killRing_.Current());
        statusMessage_.clear();
    }
    else {
        statusMessage_ = "No such character.";
    }
    EndInteractiveSession();
}

void BufferView::HandleOrgCaptureKey(const editor::KeyChord& chord) {
    if (IsQuit(chord)) {
        statusMessage_ = "Capture cancelled.";
        EndInteractiveSession();
        return;
    }
    if (!IsPlainCharacter(chord)) {
        return; // ignore, keep waiting for a template key
    }

    // Template keys are always plain ASCII (Janet callers pass a one-char
    // std::string) -- anything outside that range simply can't match.
    if (chord.Codepoint > 0x7F) {
        statusMessage_ = "No such capture template.";
        EndInteractiveSession();
        return;
    }

    const auto tmpl = editor::org::CaptureTemplateForKey(static_cast<char>(chord.Codepoint));
    if (!tmpl) {
        statusMessage_ = "No such capture template.";
        EndInteractiveSession();
        return;
    }

    try {
        const std::filesystem::path targetPath(tmpl->targetFile);
        if (targetPath.has_parent_path()) {
            std::filesystem::create_directories(targetPath.parent_path());
        }
        text::Buffer&                    target = bufferList_.OpenOrCreateFile(targetPath);
        const editor::org::CaptureResult result = editor::org::InsertCapture(target, *tmpl);
        activeBuffer_.Set(target);
        target.ClearSecondaryCursors();
        target.SetPoint(result.insertedAt);
        if (!tmpl->headline.empty() && !result.headlineFound) {
            statusMessage_ = "Headline \"" + tmpl->headline + "\" not found -- filed at end of " + tmpl->targetFile;
        }
        else {
            statusMessage_ = "Captured: " + tmpl->name;
        }
    }
    catch (const std::exception& e) {
        ReportError(e.what());
    }
    EndInteractiveSession();
}

void BufferView::RefreshFuzzyPrompt(const bufferview::FuzzyPrompt& prompt) {
    bufferview::CandidateList& list = *prompt.list;
    if (prompt.pool) {
        list.Refilter(prompt.pool(), prompt_->Text());
    }
    else {
        list.Refilter(prompt_->Text());
    }

    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(list.Empty() ? std::nullopt
                                          : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(),
                                                                                        list.Ranked(), list.Selection())));
    }
}

void BufferView::HandleFuzzyPromptKey(const bufferview::FuzzyPrompt& prompt, const editor::KeyChord& chord) {
    bufferview::CandidateList& list   = *prompt.list;
    const auto                 rerank = [&]() {
        if (prompt.pool) {
            list.Refilter(prompt.pool(), prompt_->Text());
        }
        else {
            list.Refilter(prompt_->Text());
        }
    };

    if (chord.Special == editor::SpecialKey::Enter) {
        if (!prompt.historyKey.empty()) {
            promptHistory_.Record(prompt.historyKey, prompt_->Text());
        }
        rerank();
        if (list.Empty()) {
            statusMessage_ = prompt.emptyMessage(prompt_->Text());
            EndInteractiveSession();
            return;
        }
        // Read before ending the session: ending it clears the list.
        const std::string selected = list.Selected();
        EndInteractiveSession();
        prompt.commit(selected);
        return;
    }
    if (IsQuit(chord)) {
        // Before the status message, so a prompt that previews live puts back
        // what it was showing and then reports the cancellation over the top.
        if (prompt.onCancel) {
            prompt.onCancel();
        }
        statusMessage_ = prompt.cancelMessage;
        EndInteractiveSession();
        return;
    }

    if (!prompt.historyKey.empty() && TryNavigatePromptHistory(chord, prompt.historyKey)) {
        list.SelectTop();
        RefreshFuzzyPrompt(prompt);
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        rerank();
        if (chord.Special == editor::SpecialKey::Down) {
            list.SelectNext();
        }
        else {
            list.SelectPrevious();
        }
        RefreshFuzzyPrompt(prompt);
        if (prompt.onSelectionChanged) {
            prompt.onSelectionChanged();
        }
        return;
    }

    // Typing re-snaps to the top match: the ranking just put the best answer for
    // what was typed at the top, so keeping a stale selection would fight it.
    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        promptHistoryIndex_ = kNoHistoryIndex; // editing exits history browsing
        list.SelectTop();
        RefreshFuzzyPrompt(prompt);
        if (prompt.onSelectionChanged) {
            prompt.onSelectionChanged();
        }
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

bufferview::FuzzyPrompt BufferView::ExecuteCommandPrompt() {
    return {.list          = &executeCommandList_,
            .historyKey    = "execute-command",
            .cancelMessage = "Command cancelled.",
            .emptyMessage  = [](const std::string& query) { return "No command matching \"" + query + "\""; },
            .pool          = [this] { return dispatcher_.Registry().Names(); },
            .commit        = [this](const std::string& name) {
                editor::CommandContext context = MakeContext();
                context.viewportHeight         = size().height > 0 ? static_cast<std::size_t>(size().height) : 0;
                RunCommandAndHandleOutcome(context, [&] {
                    dispatcher_.Registry().Invoke(name, context);
                    return true; // Invoke() always runs the command directly -- no Pending concept here
                }); }};
}

bufferview::FuzzyPrompt BufferView::ProjectFindFilePrompt() {
    return {.list          = &projectFindFileList_,
            .historyKey    = "project-find-file",
            .cancelMessage = "Project find file cancelled.",
            .emptyMessage  = [](const std::string& query) { return "No file matching \"" + query + "\""; },
            .commit        = [this](const std::string& selected) {
                const std::filesystem::path absolutePath = editor::ProjectRoot() / selected;
                try {
                    text::Buffer& opened = bufferList_.OpenOrCreateFile(absolutePath);
                    activeBuffer_.Set(opened);
                    statusMessage_ = "Opened " + opened.Name();
                }
                catch (const std::exception& e) {
                    ReportError(e.what());
                } }};
}

bufferview::FuzzyPrompt BufferView::FindRecentFilePrompt() {
    // Unlike ProjectFindFile's project-relative entries, these are already
    // absolute, so committing opens the selection directly.
    return {.list          = &recentFileList_,
            .historyKey    = "find-recent-file",
            .cancelMessage = "Find recent file cancelled.",
            .emptyMessage  = [](const std::string& query) { return "No recent file matching \"" + query + "\""; },
            .commit        = [this](const std::string& selected) {
                try {
                    text::Buffer& opened = bufferList_.OpenOrCreateFile(std::filesystem::path(selected));
                    activeBuffer_.Set(opened);
                    statusMessage_ = "Opened " + opened.Name();
                }
                catch (const std::exception& e) {
                    ReportError(e.what());
                } }};
}

bufferview::FuzzyPrompt BufferView::SwitchProjectPrompt() {
    return {.list          = &switchProjectList_,
            .historyKey    = "switch-project",
            .cancelMessage = "Switch project cancelled.",
            .emptyMessage  = [](const std::string& query) { return "No project matching \"" + query + "\""; },
            .pool          = [this] {
                std::vector<std::string> candidates;
                candidates.reserve(switchProjectEntries_.size());
                for (const auto& entry : switchProjectEntries_) {
                    candidates.push_back(FormatProjectEntry(entry));
                }
                return candidates; },
            .commit        = [this](const std::string& selected) {
                const auto it = std::find_if(switchProjectEntries_.begin(), switchProjectEntries_.end(),
                                             [&selected](const editor::ProjectRegistryEntry& entry) {
                                                 return FormatProjectEntry(entry) == selected;
                                             });
                if (it == switchProjectEntries_.end()) {
                    ReportError("Internal error resolving the selected project.");
                    return;
                }
                ActivateProjectAndReport(it->root); }};
}

bufferview::FuzzyPrompt BufferView::SwitchToBufferPrompt() {
    return {.list          = &switchToBufferList_,
            .historyKey    = "switch-to-buffer",
            .cancelMessage = "Switch to buffer cancelled.",
            .emptyMessage  = [](const std::string& query) { return "No buffer matching \"" + query + "\""; },
            .pool          = [this] { return text::CompleteBufferNames(bufferList_, ""); },
            .commit        = [this](const std::string& selected) {
                if (text::Buffer* found = bufferList_.Find(selected)) {
                    activeBuffer_.Set(*found);
                    statusMessage_.clear();
                }
                else {
                    ReportError("Internal error resolving the selected buffer.");
                } }};
}

bufferview::FuzzyPrompt BufferView::AcpAgentNamePrompt() {
    return {.list          = &acpAgentNameList_,
            .historyKey    = "acp-agent-name",
            .cancelMessage = "Start ACP session cancelled.",
            .emptyMessage  = [](const std::string& query) { return "No agent matching \"" + query + "\""; },
            .pool          = [] { return editor::acp::AcpAgentNames(); },
            .commit        = [this](const std::string& selected) {
                if (!acpManager_) {
                    statusMessage_ = "No ACP manager available.";
                }
                else if (text::Buffer* buffer = acpManager_->StartSession(selected)) {
                    activeBuffer_.Set(*buffer);
                    statusMessage_.clear();
                } }};
}

bufferview::FuzzyPrompt BufferView::BookmarkJumpPrompt() {
    const bool isDelete = (bookmarkPromptAction_ == BookmarkPromptAction::Delete);
    return {.list          = &bookmarkList_,
            .historyKey    = "bookmark-jump",
            .cancelMessage = std::string(isDelete ? "Delete bookmark" : "Jump to bookmark") + " cancelled.",
            .emptyMessage  = [](const std::string& query) { return "No bookmark matching \"" + query + "\""; },
            .commit        = [this, isDelete](const std::string& selected) {
                if (isDelete) {
                    editor::DeleteBookmark(selected);
                    editor::SaveBookmarks();
                    statusMessage_ = "Deleted bookmark: " + selected;
                    return;
                }
                const std::optional<editor::Bookmark> mark = editor::FindBookmark(selected);
                if (!mark) {
                    statusMessage_ = "Bookmark \"" + selected + "\" no longer exists";
                    return;
                }
                try {
                    text::Buffer& opened = bufferList_.OpenOrCreateFile(mark->path);
                    PushJumpMark();
                    activeBuffer_.Set(opened);
                    opened.SetPoint(opened.ByteOffsetForLineAndColumn(mark->line, mark->column,
                                                                      static_cast<std::size_t>(editor::TabWidth())));
                    statusMessage_ = "Bookmark: " + selected;
                }
                catch (const std::exception& e) {
                    ReportError(e.what());
                } }};
}

bufferview::FuzzyPrompt BufferView::SelectThemePrompt() {
    // The snapshot is captured here rather than read inside commit, because the
    // driver ends the session -- which clears themeBeforePreview_ -- before
    // commit runs.
    return {.list          = &selectThemeList_,
            .historyKey    = {}, // a fixed list of names; nothing gained by remembering what was typed
            .cancelMessage = "Theme selection cancelled.",
            .emptyMessage  = [](const std::string& query) { return "No theme matching \"" + query + "\""; },
            .commit =
                [this, snapshot = themeBeforePreview_](const std::string& selected) {
                    // Usually the highlighted candidate is already applied (every
                    // selection change previews), but an immediate Enter on a fresh
                    // session never previewed anything; applying explicitly covers
                    // that and is a no-op re-apply otherwise.
                    if (selected == kCurrentThemeLabel) {
                        // Committing "Current theme" leaves everything as it is: no
                        // ThemeByName() lookup (there is no registry entry by this
                        // name) and no variables.json write, since the persisted base
                        // theme name already describes what is active and overwriting
                        // it with this synthetic label would break the next launch's
                        // own theme resolution.
                        if (themeApplier_ && snapshot) {
                            themeApplier_(*snapshot);
                        }
                        statusMessage_ = "Theme unchanged.";
                        return;
                    }
                    if (themeApplier_) {
                        if (const auto named = ThemeByName(selected)) {
                            themeApplier_(*named);
                        }
                    }
                    // A committed pick is remembered across runs as the *base* theme;
                    // preview and cancel deliberately never persist anything, and
                    // init.janet's own (ned/theme-set ...) overrides still apply over
                    // it at startup.
                    editor::SetVariable("theme", selected);
                    statusMessage_ = "Theme: " + selected;
                },
            .onSelectionChanged = [this] { ApplySelectedThemePreview(); },
            .onCancel           = [this] {
                if (themeApplier_ && themeBeforePreview_) {
                    themeApplier_(*themeBeforePreview_);
                } }};
}

void BufferView::RefreshExecuteCommandStatus() {
    RefreshFuzzyPrompt(ExecuteCommandPrompt());
}

void BufferView::HandleExecuteCommandKey(const editor::KeyChord& chord) {
    HandleFuzzyPromptKey(ExecuteCommandPrompt(), chord);
}

void BufferView::RefreshProjectFindFileStatus() {
    RefreshFuzzyPrompt(ProjectFindFilePrompt());
}

void BufferView::HandleProjectFindFileKey(const editor::KeyChord& chord) {
    HandleFuzzyPromptKey(ProjectFindFilePrompt(), chord);
}

// editor-ergonomics follow-up: HandleProjectFindFileKey/
// RefreshProjectFindFileStatus's own shape, over recentFileCandidates_
// (already-absolute paths, unlike ProjectFindFile's project-relative ones,
// so Enter opens the selection directly with no ProjectRoot() join).

void BufferView::RefreshFindRecentFileStatus() {
    RefreshFuzzyPrompt(FindRecentFilePrompt());
}

void BufferView::HandleFindRecentFileKey(const editor::KeyChord& chord) {
    HandleFuzzyPromptKey(FindRecentFilePrompt(), chord);
}

// named-projects follow-up: HandleProjectFindFileKey/RefreshProjectFindFileStatus's
// own shape, over switchProjectEntries_ (Editor/ProjectRegistry.h's saved-project
// list) formatted via FormatProjectEntry.

void BufferView::RefreshSwitchProjectStatus() {
    RefreshFuzzyPrompt(SwitchProjectPrompt());
}

void BufferView::HandleSwitchProjectKey(const editor::KeyChord& chord) {
    HandleFuzzyPromptKey(SwitchProjectPrompt(), chord);
}

// dropdown-path-completion follow-up: HandleSwitchProjectKey's own shape,
// over every open buffer's name (text::CompleteBufferNames(bufferList_, "")
// -- the full list, fuzzy-ranked here rather than prefix-filtered). Unlike
// find-file/find-scratch, there's no "create a new buffer by typing a name
// that doesn't exist" case (the old CompletePrompt-driven prompt reported an
// error on a non-match rather than creating anything), so Enter safely
// resolves the highlighted ranked candidate instead of literal prompt text.

void BufferView::RefreshSwitchToBufferStatus() {
    RefreshFuzzyPrompt(SwitchToBufferPrompt());
}

void BufferView::HandleSwitchToBufferKey(const editor::KeyChord& chord) {
    HandleFuzzyPromptKey(SwitchToBufferPrompt(), chord);
}

// dropdown-path-completion follow-up: RefreshSwitchToBufferStatus's own
// shape, over vcsBranchCandidates_ (populated once, before this mode is
// entered -- see BeginVcsSwitchBranchPrompt). VcsCreateBranch is a distinct
// InputMode (free-text new-branch naming, no candidate list) and is not
// handled here -- it stays on HandlePromptKey's own literal-text path.

void BufferView::RefreshAcpAgentNameStatus() {
    RefreshFuzzyPrompt(AcpAgentNamePrompt());
}

void BufferView::HandleAcpAgentNameKey(const editor::KeyChord& chord) {
    HandleFuzzyPromptKey(AcpAgentNamePrompt(), chord);
}

// named-projects follow-up: the shared tail of switch-project/open-project
// once a target root is known -- see this method's own header-comment
// contract in BufferView.h.

void BufferView::ActivateProjectAndReport(const std::filesystem::path& root) {
    switch (editor::ActivateProjectRoot(root)) {
        case editor::ProjectActivationOutcome::OpenedInNewTab:
            statusMessage_ = "Opened " + root.string() + " in a new tab.";
            return;
        case editor::ProjectActivationOutcome::RanCustomCommand:
            statusMessage_ = "Opened " + root.string() + " via the configured open command.";
            return;
        case editor::ProjectActivationOutcome::ReplacingInPlace:
            // HandleConfirmQuitKey's own 'y' branch: a visible message
            // before quitting, then eventLoop_->Exit() directly -- ned's
            // own main.cpp performs the actual execv() once every local
            // there (this BufferView included) has been destroyed.
            statusMessage_ = "Switching to " + root.string() + "...";
            if (eventLoop_) {
                eventLoop_->Exit();
            }
            return;
        case editor::ProjectActivationOutcome::Failed:
            statusMessage_ = "Could not switch to " + root.string() +
                             " -- no terminal detected, no configured open command, and this "
                             "process's own executable path couldn't be resolved.";
            return;
        case editor::ProjectActivationOutcome::RootMissing:
            statusMessage_ = root.string() + " no longer exists.";
            return;
    }
}

// editor-ergonomics follow-up: same picker shape again, over
// bookmarkCandidates_ (Editor/Bookmark.h's sorted name list). Enter jumps
// (bookmarkPromptAction_ == Jump) or deletes (== Delete) the selected name.

void BufferView::RefreshBookmarkJumpStatus() {
    RefreshFuzzyPrompt(BookmarkJumpPrompt());
}

void BufferView::HandleBookmarkJumpKey(const editor::KeyChord& chord) {
    HandleFuzzyPromptKey(BookmarkJumpPrompt(), chord);
}

// rich-theme-set follow-up (Phase 1) -- see the declarations' own doc
// comments in BufferView.h for the session's overall shape.

void BufferView::RefreshSelectThemeStatus() {
    RefreshFuzzyPrompt(SelectThemePrompt());
}

void BufferView::ApplySelectedThemePreview() {
    if (!themeApplier_) {
        return;
    }
    const std::vector<std::string>& ranked = selectThemeList_.Refiltered(prompt_->Text());
    if (ranked.empty()) {
        // Nothing highlighted to preview -- show what the session started
        // on rather than leaving whichever candidate was last previewed.
        if (themeBeforePreview_) {
            themeApplier_(*themeBeforePreview_);
        }
        return;
    }
    const std::string& selected = ranked[std::min(selectThemeList_.Selection(), ranked.size() - 1)];
    if (selected == kCurrentThemeLabel) {
        // select-theme-current-row follow-up: resolved against the
        // snapshot, not ThemeByName() -- see kCurrentThemeLabel's own doc
        // comment for why a named lookup here would be destructive.
        if (themeBeforePreview_) {
            themeApplier_(*themeBeforePreview_);
        }
        return;
    }
    if (const auto named = ThemeByName(selected)) {
        themeApplier_(*named);
    }
}

void BufferView::HandleSelectThemeKey(const editor::KeyChord& chord) {
    HandleFuzzyPromptKey(SelectThemePrompt(), chord);
}

// ListPopup-mouse-support-remainder follow-up: see this method's own doc
// comment in BufferView.h for the overall shape. Each fuzzy-ranked branch
// recomputes the same ranked/candidate list its own Refresh*Status does --
// safe because nothing about the session (prompt_->Text(), the candidate
// source) changes between a popup render and a click landing on one of its
// rows -- resolves the clicked row via ResolveFuzzyCandidateRowIndex, sets
// that session's own selection member, and re-dispatches a synthetic Enter
// through its existing Handle*Key rather than duplicating that method's
// commit logic.

void BufferView::ActivateCandidatePopupAt(std::size_t index) {
    const editor::KeyChord enter{.Special = editor::SpecialKey::Enter};

    switch (inputMode_) {
        case InputMode::ExecuteCommand: {
            const std::vector<std::string>& ranked =
                executeCommandList_.Refiltered(dispatcher_.Registry().Names(), prompt_->Text());
            const auto resolved = ResolveFuzzyCandidateRowIndex(index, executeCommandList_.Selection(), ranked.size());
            if (!resolved) {
                return;
            }
            executeCommandList_.SelectIndex(*resolved);
            HandleExecuteCommandKey(enter);
            return;
        }
        case InputMode::FindRecentFile: {
            const std::vector<std::string>& ranked   = recentFileList_.Refiltered(prompt_->Text());
            const auto                      resolved = ResolveFuzzyCandidateRowIndex(index, recentFileList_.Selection(), ranked.size());
            if (!resolved) {
                return;
            }
            recentFileList_.SelectIndex(*resolved);
            HandleFindRecentFileKey(enter);
            return;
        }
        case InputMode::ProjectFindFile: {
            const std::vector<std::string>& ranked   = projectFindFileList_.Refiltered(prompt_->Text());
            const auto                      resolved = ResolveFuzzyCandidateRowIndex(index, projectFindFileList_.Selection(), ranked.size());
            if (!resolved) {
                return;
            }
            projectFindFileList_.SelectIndex(*resolved);
            HandleProjectFindFileKey(enter);
            return;
        }
        case InputMode::SwitchProject: {
            std::vector<std::string> candidates;
            candidates.reserve(switchProjectEntries_.size());
            for (const auto& entry : switchProjectEntries_) {
                candidates.push_back(FormatProjectEntry(entry));
            }
            const std::vector<std::string>& ranked   = switchProjectList_.Refiltered(candidates, prompt_->Text());
            const auto                      resolved = ResolveFuzzyCandidateRowIndex(index, switchProjectList_.Selection(), ranked.size());
            if (!resolved) {
                return;
            }
            switchProjectList_.SelectIndex(*resolved);
            HandleSwitchProjectKey(enter);
            return;
        }
        case InputMode::SwitchToBuffer: {
            const std::vector<std::string>  candidates = text::CompleteBufferNames(bufferList_, "");
            const std::vector<std::string>& ranked     = switchToBufferList_.Refiltered(candidates, prompt_->Text());
            const auto                      resolved   = ResolveFuzzyCandidateRowIndex(index, switchToBufferList_.Selection(), ranked.size());
            if (!resolved) {
                return;
            }
            switchToBufferList_.SelectIndex(*resolved);
            HandleSwitchToBufferKey(enter);
            return;
        }
        case InputMode::VcsSwitchBranch: {
            const std::vector<std::string>& ranked   = vcsBranchList_.Refiltered(prompt_->Text());
            const auto                      resolved = ResolveFuzzyCandidateRowIndex(index, vcsBranchList_.Selection(), ranked.size());
            if (!resolved) {
                return;
            }
            vcsBranchList_.SelectIndex(*resolved);
            HandleVcsSwitchBranchKey(enter);
            return;
        }
        case InputMode::BookmarkJump: {
            const std::vector<std::string>& ranked   = bookmarkList_.Refiltered(prompt_->Text());
            const auto                      resolved = ResolveFuzzyCandidateRowIndex(index, bookmarkList_.Selection(), ranked.size());
            if (!resolved) {
                return;
            }
            bookmarkList_.SelectIndex(*resolved);
            HandleBookmarkJumpKey(enter);
            return;
        }
        case InputMode::SelectTheme: {
            const std::vector<std::string>& ranked   = selectThemeList_.Refiltered(prompt_->Text());
            const auto                      resolved = ResolveFuzzyCandidateRowIndex(index, selectThemeList_.Selection(), ranked.size());
            if (!resolved) {
                return;
            }
            selectThemeList_.SelectIndex(*resolved);
            HandleSelectThemeKey(enter);
            return;
        }
        case InputMode::LspGotoSymbol: {
            const auto resolved = ResolveFuzzyCandidateRowIndex(index, documentSymbolSelection_, documentSymbolLabels_.size());
            if (!resolved) {
                return;
            }
            documentSymbolSelection_ = *resolved;
            HandleDocumentSymbolKey(enter);
            return;
        }
        case InputMode::LspWorkspaceSymbol: {
            const auto resolved =
                ResolveFuzzyCandidateRowIndex(index, workspaceSymbolSelection_, pendingWorkspaceSymbols_.size());
            if (!resolved) {
                return;
            }
            workspaceSymbolSelection_ = *resolved;
            HandleWorkspaceSymbolKey(enter);
            return;
        }
        case InputMode::LspCodeActionSelect: {
            // No window/divider rows here -- RefreshCodeActionSelectStatus
            // builds a plain 1:1 numbered list, unlike the fuzzy-ranked
            // sessions above, so the clicked row is already the real index.
            if (index >= pendingCodeActions_.size()) {
                return;
            }
            codeActionSelection_ = index;
            HandleCodeActionSelectKey(enter);
            return;
        }
        case InputMode::FindFile:
        case InputMode::OpenProjectPath:
        case InputMode::FindScratch: {
            // Tab's own behavior, not Enter's: these three sessions finalize
            // on literal prompt_->Text(), so a click fills the prompt from
            // the candidate rather than submitting it -- see
            // RefreshPathCompletionPopup's own doc comment.
            const std::vector<std::string> candidates = GatherPathCompletionCandidates();
            const auto                     resolved   = ResolveFuzzyCandidateRowIndex(index, pathCompletionSelection_, candidates.size());
            if (!resolved) {
                return;
            }
            prompt_->SetText(candidates[*resolved]);
            pathCompletionSelection_ = 0;
            RefreshPathCompletionPopup();
            return;
        }
        default:
            return; // no candidate popup active for this mode (a stale click racing an already-ended session)
    }
}

void BufferView::ScrollCandidatePopup(int steps) {
    if (steps == 0) {
        return;
    }
    const editor::KeyChord nav{.Special = steps > 0 ? editor::SpecialKey::Down : editor::SpecialKey::Up};
    const int              count = steps > 0 ? steps : -steps;
    for (int i = 0; i < count; ++i) {
        switch (inputMode_) {
            case InputMode::ExecuteCommand:
                HandleExecuteCommandKey(nav);
                break;
            case InputMode::ProjectFindFile:
                HandleProjectFindFileKey(nav);
                break;
            case InputMode::FindRecentFile:
                HandleFindRecentFileKey(nav);
                break;
            case InputMode::SwitchProject:
                HandleSwitchProjectKey(nav);
                break;
            case InputMode::SwitchToBuffer:
                HandleSwitchToBufferKey(nav);
                break;
            case InputMode::VcsSwitchBranch:
                HandleVcsSwitchBranchKey(nav);
                break;
            case InputMode::AcpAgentName:
                HandleAcpAgentNameKey(nav);
                break;
            case InputMode::BookmarkJump:
                HandleBookmarkJumpKey(nav);
                break;
            case InputMode::SelectTheme:
                HandleSelectThemeKey(nav);
                break;
            case InputMode::LspGotoSymbol:
                HandleDocumentSymbolKey(nav);
                break;
            case InputMode::LspWorkspaceSymbol:
                HandleWorkspaceSymbolKey(nav);
                break;
            case InputMode::LspCodeActionSelect:
                HandleCodeActionSelectKey(nav);
                break;
            case InputMode::FindFile:
            case InputMode::OpenProjectPath:
            case InputMode::FindScratch:
                HandlePromptKey(nav);
                break;
            default:
                return; // no candidate popup active for this mode -- nothing to scroll
        }
    }
}

void BufferView::SetThemeApplier(std::function<void(const Theme&)> applier) {
    themeApplier_ = std::move(applier);
}

void BufferView::SetAcpManager(editor::acp::AcpManager* acpManager) {
    acpManager_ = acpManager;
}

void BufferView::ShowAcpPermissionPrompt(const editor::acp::AcpManager::PermissionPrompt& prompt) {
    pendingAcpPermissionOptions_ = prompt.options;
    acpPermissionSelection_      = 0;
    inputMode_                   = InputMode::AcpPermissionPrompt;
    acpPermissionDescription_    = prompt.description;
    RefreshAcpPermissionPromptStatus();
}

void BufferView::RefreshAcpPermissionPromptStatus() {
    std::string status = acpPermissionDescription_ + ": ";
    for (std::size_t i = 0; i < pendingAcpPermissionOptions_.size(); ++i) {
        if (i > 0) {
            status += "  ";
        }
        const bool selected = (i == acpPermissionSelection_);
        status += (selected ? "[" : "") + std::to_string(i + 1) + ") " + pendingAcpPermissionOptions_[i].name + (selected ? "]" : "");
    }
    statusMessage_ = status;
}

void BufferView::HandleAcpPermissionPromptKey(const editor::KeyChord& chord) {
    HandleChoicePromptKey({.count         = pendingAcpPermissionOptions_.size(),
                           .selection     = &acpPermissionSelection_,
                           .cancelMessage = "Permission request dismissed.",
                           .refresh       = [this] { RefreshAcpPermissionPromptStatus(); },
                           // Captured: ending the session clears the pending options.
                           .commit =
                               [this, options = pendingAcpPermissionOptions_](std::size_t index) {
                                   const editor::acp::AcpManager::PermissionOption& option = options[index];
                                   if (acpManager_) {
                                       acpManager_->ResolvePermissionPrompt(option.optionId);
                                   }
                                   statusMessage_ = "Selected \"" + option.name + "\".";
                               }},
                          chord);
}

} // namespace ned::ui
