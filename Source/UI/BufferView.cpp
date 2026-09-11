//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// Construction, event entry points, key dispatch, macro replay, and the Set* wiring hooks.
//

#include "UI/BufferView/Internal.h"

namespace ned::ui {

// The file-local helpers these definitions call live in BufferView/Internal.h
// now that several parts share them -- see that header. This using-directive is
// what let the split leave every call site untouched.
using namespace detail;

BufferView::BufferView(ActiveBuffer& activeBuffer, text::KillRing& killRing, editor::RegisterTable& registers,
                       editor::PromptHistory& promptHistory, text::BufferList& bufferList, editor::Dispatcher& dispatcher,
                       std::string& statusMessage, const editor::Mode& mode, const Theme& theme) : activeBuffer_(activeBuffer), killRing_(killRing), registers_(registers),
                                                                                                   promptHistory_(promptHistory), bufferList_(bufferList),
                                                                                                   dispatcher_(dispatcher), statusMessage_(statusMessage), mode_(mode), theme_(theme),
                                                                                                   context_{activeBuffer_, killRing_, registers_, promptHistory_, bufferList_,
                                                                                                            dispatcher_, statusMessage_, mode_, theme_, lspManager_, dapManager_,
                                                                                                            acpManager_, vcsRunner_, taskRunner_, testRunner_, projectUndo_,
                                                                                                            eventLoop_, janetEnv_},
                                                                                                   gutters_(context_,
                                                                                                            [this](const text::ITextStorage& content) {
                                                                                                                return viewport_.HugeStructuralWindow(content);
                                                                                                            }),
                                                                                                   viewport_(context_, gutters_,
                                                                                                             bufferview::Viewport::Host{
                                                                                                                 [this]() { return size(); },
                                                                                                                 [this]() { return GutterWidth(); },
                                                                                                                 [this]() { return stickyRowCount_; },
                                                                                                                 [this](std::size_t line) { return AnnotationRowsForLine(line); },
                                                                                                                 [this](std::size_t line) { return LeadingAnnotationRowsForLine(line); },
                                                                                                                 [this](std::size_t lineStart, std::size_t lineEnd) {
                                                                                                                     return InlayHintsForLineRange(lineStart, lineEnd);
                                                                                                                 },
                                                                                                                 [this]() { DismissHover(); }}) {
    if (const char* path = std::getenv("NED_DEBUG_MOUSE"); path && *path) {
        debugMouseLogPath_ = path;
    }
    // The buffer active at construction time already has a sensible
    // viewport_.TopLine() (0, its default) -- seeding this here rather than leaving it
    // nullptr is what makes viewport_.EnsureTopLineValidForActiveBuffer() correctly
    // distinguish "this is a real switch to a different buffer" from "this
    // is just the very first Paint() call," which would otherwise
    // The buffer this pane starts on gets its remembered viewport applied and
    // its scroll position seeded -- see Viewport::RestoreInitialPlace.
    viewport_.RestoreInitialPlace();
    // Same reasoning as Viewport::RestoreInitialPlace's own seeding, for
    // onActiveBufferChanged_: the buffer active at construction is already
    // reflected in whatever Mode the owning Pane constructed this
    // BufferView with, so the first Paint() must not re-fire the callback.
    modeSyncBuffer_ = &activeBuffer_.Get();
}

// The scroll position lives in Viewport now; these stay on BufferView because
// an externally-owned ScrollBar/Minimap is wired to them by the pane that owns
// both, and has no business reaching past the widget for it.
std::size_t BufferView::TopLine() const {
    return viewport_.TopLine();
}

void BufferView::SetTopLine(std::size_t line) {
    viewport_.SetTopLine(line);
}

std::size_t BufferView::LeftColumn() const {
    return viewport_.LeftColumn();
}

void BufferView::SetLeftColumn(std::size_t column) {
    viewport_.SetLeftColumn(column);
}

editor::CommandContext BufferView::MakeContext() {
    editor::CommandContext context{context_.activeBuffer.Get(), context_.killRing, context_.bufferList,
                                   editor::KeyChord{}, &context_.statusMessage};
    context.mode        = &context_.mode;
    context.lspManager  = context_.lspManager;
    context.taskRunner  = context_.taskRunner;
    context.testRunner  = context_.testRunner;
    context.projectUndo = context_.projectUndo;
    return context;
}

void BufferView::SetOnWindowRequest(std::function<void(editor::InteractiveRequest)> handler) {
    onWindowRequest_ = std::move(handler);
}

void BufferView::SetSplitResizeQuery(std::function<bool()> query) {
    splitResizeQuery_ = std::move(query);
}

void BufferView::SetOnBufferClosed(std::function<void(text::Buffer&)> handler) {
    onBufferClosed_ = std::move(handler);
}

void BufferView::SetOnTerminalToggle(std::function<void()> handler) {
    onTerminalToggle_ = std::move(handler);
}

void BufferView::SetOnNewTerminalRequest(std::function<void()> handler) {
    onNewTerminalRequest_ = std::move(handler);
}

void BufferView::SetOnJanetReplToggle(std::function<void()> handler) {
    onJanetReplToggle_ = std::move(handler);
}

void BufferView::SetOnRunReplRequest(std::function<void(const std::string&)> handler) {
    onRunReplRequest_ = std::move(handler);
}

void BufferView::SetOnBufferListToggle(std::function<void()> handler) {
    onBufferListToggle_ = std::move(handler);
}

void BufferView::SetOnThemeGalleryToggle(std::function<void()> handler) {
    onThemeGalleryToggle_ = std::move(handler);
}

void BufferView::SetOnActiveBufferChanged(std::function<void(text::Buffer&)> handler) {
    onActiveBufferChanged_ = std::move(handler);
}

void BufferView::SetOnPrefixHintChanged(std::function<void(std::optional<WhichKeyHint>)> handler) {
    onPrefixHintChanged_ = std::move(handler);
}

void BufferView::ClearBufferCaches(text::Buffer& buffer) {
    highlightCacheByBuffer_.erase(&buffer);
    embeddedDocumentCacheByBuffer_.erase(&buffer);
    if (highlightCacheStamp_.IsFor(&buffer)) {
        highlightCacheStamp_.Invalidate();
        highlightCacheSpans_.clear();
    }
    gutters_.ForgetBuffer(buffer);
    // rename-review follow-up: the proposal table is keyed by raw Buffer*
    // identity like every cache above it, so it has to be dropped on the
    // same funnel -- a later buffer landing at the same address would
    // otherwise inherit another rename's proposals.
    ClearRenameProposals(buffer);
}

std::optional<std::string> BufferView::EmbeddedLanguageAtPoint() {
    EnsureEmbeddedDocumentCache();
    text::Buffer& buffer = activeBuffer_.Get();
    const auto    it     = embeddedDocumentCacheByBuffer_.find(&buffer);
    if (it == embeddedDocumentCacheByBuffer_.end()) {
        return std::nullopt;
    }
    return editor::EmbeddedLanguageAtByteOffset(it->second.documents, buffer.Point());
}

void BufferView::NextError() {
    StepError(/*forward=*/true);
}

void BufferView::PreviousError() {
    StepError(/*forward=*/false);
}

void BufferView::StepError(bool forward) {
    const std::optional<std::string> name = editor::LastResultsBuffer();
    if (!name) {
        statusMessage_ = "No results to step through.";
        return;
    }
    text::Buffer* resultsBuffer = bufferList_.Find(*name);
    if (!resultsBuffer) {
        statusMessage_ = "No results to step through.";
        return;
    }
    const std::vector<editor::ErrorLocation> locations = editor::CollectResultLocations(*resultsBuffer);
    if (locations.empty()) {
        statusMessage_ = "No results to step through.";
        return;
    }

    // Editor/NextError.h's StepResultLocation owns the actual walk cursor
    // (index-based, process-wide, reset whenever a fresh results buffer is
    // registered) -- see its own doc comment for why this can't just
    // compare against resultsBuffer's Point().
    const std::optional<editor::ErrorLocation> next = editor::StepResultLocation(locations, forward);
    if (!next) {
        statusMessage_ = forward ? "No more errors below point." : "No more errors above point.";
        return;
    }
    JumpToPathLine(next->sourcePath, next->sourceLine);
}

bool BufferView::Focusable() const {
    return true; // was FocusPolicy::Strong
}

bool BufferView::OnEvent(const Event& event) {
    if (event.is_mouse()) {
        return OnMouseEvent(event);
    }
    return OnKeyEvent(event);
}

bool BufferView::OnKeyEvent(const Event& event) {
    const auto chord = TranslateKey(event);
    if (!chord) {
        return false;
    }
    DismissHover(); // hover-tooltips follow-up: any real keystroke ends a pending/shown tooltip

    // One switch rather than a chain of ifs: with no default label the
    // compiler reports any InputMode that is not dispatched, which is exactly
    // the mistake that has twice shipped as a prompt whose keystrokes fell
    // through to self-insert. Every case returns; Normal is the only one that
    // falls out to the ordinary key handling below.
    switch (inputMode_) {
        case InputMode::Normal:
            break; // ordinary editing -- handled below

        case InputMode::IsearchForward:
        case InputMode::IsearchBackward:
            HandleSearchKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::QueryReplace:
            HandleQueryReplaceKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::ProjectReplace:
            HandleProjectReplaceKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::ConfirmQuit:
            HandleConfirmQuitKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::ConfirmCloseBuffer:
            HandleConfirmCloseBufferKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::ConfirmWriteThemeToInit:
            HandleConfirmWriteThemeToInitKey(*chord);
            ClampPointToNarrowing();
            return true;
        case InputMode::ConfirmOverwriteSave:
            HandleConfirmOverwriteSaveKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::MultibufferApplyTarget:
            HandleMultibufferApplyTargetKey(*chord);
            return true;
        case InputMode::ConfirmSaveWithConflicts:
            HandleConfirmSaveWithConflictsKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::ConfirmRevertHunk:
            HandleConfirmRevertHunkKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::ConfirmRenameFileToMatchType:
            HandleConfirmRenameFileToMatchTypeKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::ConfirmRenameTypeToMatchFile:
            HandleConfirmRenameTypeToMatchFileKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::ConfirmOpenBinary:
            HandleConfirmOpenBinaryKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::ConfirmTrustProjectInit:
            HandleConfirmTrustProjectInitKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::FindFile:
        case InputMode::OpenProjectPath:
        case InputMode::FindScratch:
        case InputMode::BookmarkSetName:
        case InputMode::OpenProjectName:
        case InputMode::TaskName:
        case InputMode::AcpPromptText:
        case InputMode::CreateDirectory:
        case InputMode::DapAddWatch:
        case InputMode::DapBreakpointCondition:
        case InputMode::DapBreakpointHitCondition:
        case InputMode::DapBreakpointLogMessage:
        case InputMode::DapEvaluate:
        case InputMode::DapFunctionBreakpointName:
        case InputMode::DapMemoryByteCount:
        case InputMode::DapSetVariableValue:
        case InputMode::DeleteProperty:
        case InputMode::GotoLine:
        case InputMode::LspRenameNewName:
        case InputMode::RenameLocalNewName:
        case InputMode::OrgDeadline:
        case InputMode::OrgSchedule:
        case InputMode::ProjectSearch:
        case InputMode::ReplName:
        case InputMode::SearchInResults:
        case InputMode::SetHeadlineTags:
        case InputMode::ShowMassifGraphPath:
        case InputMode::StringRectangle:
        case InputMode::VcsCreateBranch:
            // Every plain text-entry prompt. What Tab offers in each, and what
            // it is called when cancelled, is TextEntryPromptFor's business.
            HandlePromptKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::DeleteFile:
            HandleDeleteFileKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::RenameFile:
            HandleRenameFileKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::SetProperty:
            HandleSetPropertyKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::RecoverFile:
            HandleRecoverFileKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::ExecuteCommand:
            // No ClampPointToNarrowing() here: HandleExecuteCommandKey's own
            // Enter branch already routes through RunCommandAndHandleOutcome
            // internally (M-x invoking a command by name), which handles the
            // clamp itself -- see that method's own doc comment for why it has
            // to be the one doing it, not a caller after the fact.
            HandleExecuteCommandKey(*chord);
            return true;

        case InputMode::ProjectFindFile:
            // Unlike ExecuteCommand, Enter here just opens a file directly
            // (BufferList::OpenOrCreateFile + activeBuffer_.Set()) rather than
            // routing through RunCommandAndHandleOutcome, so the ordinary
            // after-the-fact ClampPointToNarrowing() every other prompt-shaped
            // mode uses is correct here too.
            HandleProjectFindFileKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::FindRecentFile:
            HandleFindRecentFileKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::SwitchProject:
            HandleSwitchProjectKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::SwitchToBuffer:
            HandleSwitchToBufferKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::VcsSwitchBranch:
            HandleVcsSwitchBranchKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::AcpAgentName:
            HandleAcpAgentNameKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::BookmarkJump:
            HandleBookmarkJumpKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::SelectTheme:
            HandleSelectThemeKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::PointToRegister:
        case InputMode::JumpToRegister:
        case InputMode::CopyToRegister:
        case InputMode::InsertRegister:
            HandleRegisterKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::ZapToChar:
            HandleZapToCharKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::OrgCaptureSelectTemplate:
            HandleOrgCaptureKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::AcpPermissionPrompt:
            HandleAcpPermissionPromptKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::DapThreadSelect:
            HandleDapThreadSelectKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::DapExceptionFilterSelect:
            HandleDapExceptionFilterSelectKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::LspCodeActionSelect:
            HandleCodeActionSelectKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::LspGotoDefinitionSelect:
            HandleDefinitionSelectKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::LspPeekDefinition:
            HandlePeekDefinitionKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::ContextMenu:
            HandleContextMenuKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::LspGotoSymbol:
            // Same "Enter jumps directly, no RunCommandAndHandleOutcome routing"
            // shape as ProjectFindFile above.
            HandleDocumentSymbolKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::LspWorkspaceSymbol:
            HandleWorkspaceSymbolKey(*chord);
            ClampPointToNarrowing();
            return true;

        case InputMode::PrefixArgument:
            // No ClampPointToNarrowing(): reading a prefix argument never moves
            // point itself -- a Continue outcome does nothing to the buffer, and
            // a Terminate outcome re-dispatches through DispatchChordNormally,
            // which already runs the same clamp any other normal dispatch does.
            HandlePrefixArgumentKey(*chord);
            return true;

        case InputMode::Snippet:
            // No ClampPointToNarrowing() here for the same PrefixArgument
            // reason: consumed chords clamp inside HandleSnippetKey themselves,
            // and a fall-through chord re-dispatches through
            // DispatchChordNormally, after which *this* may be destroyed.
            HandleSnippetKey(*chord);
            return true;
    }

    // completion-popup follow-up (was hover/completion follow-up): completion
    // state only ever exists while inputMode_ == Normal (every branch above
    // returns before reaching here), so this is the one place it needs
    // handling -- Tab accepts, Up/Down/M-n/M-p cycle (arrows for the popup's
    // own standard navigation, M-n/M-p kept for existing muscle memory --
    // plain arrows are otherwise free here, multi-cursor's add-cursor-above/
    // -below use C-Up/C-Down), and (falling through) any other key dismisses
    // it, then continues to whatever that key would ordinarily do. Checked
    // ahead of the normal dispatch below so none of these ever reach
    // Dispatcher::Feed while the popup is showing.
    if (activeCompletion_) {
        if (chord->Special == editor::SpecialKey::Tab && !chord->Control && !chord->Meta) {
            AcceptActiveCompletion();
            ClampPointToNarrowing();
            return true;
        }
        if ((chord->Special == editor::SpecialKey::Down && !chord->Control && !chord->Meta) ||
            (chord->Meta && !chord->Control && chord->Codepoint == U'n')) {
            CycleActiveCompletion(1);
            return true;
        }
        if ((chord->Special == editor::SpecialKey::Up && !chord->Control && !chord->Meta) ||
            (chord->Meta && !chord->Control && chord->Codepoint == U'p')) {
            CycleActiveCompletion(-1);
            return true;
        }
        // completion-trigger-characters follow-up: a commit character
        // accepts the selected item and *then* inserts itself -- typing "("
        // after a function name completes the name and keeps the paren.
        // Only characters the server (or the item) actually declared count:
        // this client never substitutes a default set, so an editor talking
        // to a server that declares none behaves exactly as it did before
        // commitCharacters was parsed at all -- but the servers that do
        // declare them are not shy: typescript-language-server sends
        // {".", ",", ";", "("} on every item, so ned/set-lsp-commit-characters
        // exists to turn the whole behavior off (see its own doc comment in
        // ServerConfig.h).
        if (chord->Special == editor::SpecialKey::None && !chord->Control && !chord->Meta &&
            editor::lsp::CommitCharactersEnabled() &&
            activeCompletion_->IsCommitCharacter(text::EncodeCodepointUtf8(chord->Codepoint))) {
            AcceptActiveCompletion();
            ClampPointToNarrowing();
            // Falls through to the ordinary dispatch below rather than
            // returning, so the character itself still self-inserts -- that
            // is the whole point of a commit character, as opposed to Tab.
        }
        // completion-fidelity follow-up: a keystroke that edits the word
        // being completed no longer dismisses the popup here -- it falls
        // through to the ordinary dispatch below and then to
        // MaybeScheduleAutoCompletion, which narrows the existing candidate
        // set locally rather than dropping it and paying a whole new round
        // trip per character (the "popup blinks out between keystrokes"
        // behavior this replaced). Backspace is included deliberately:
        // CompletionSession keeps every item the server sent, so widening
        // back toward the original prefix is free and needs no request.
        // Everything else (motion, C-/M- chords, Enter, ...) still dismisses.
        const bool editsCompletionWord = (!chord->Control && !chord->Meta) &&
                                         (chord->Special == editor::SpecialKey::None ||
                                          chord->Special == editor::SpecialKey::Backspace);
        if (!editsCompletionWord) {
            activeCompletion_.reset();
            NotifyCompletionChanged();
        }
    }

    // project-search-visit-result follow-up: Enter on a read-only
    // ("tossable") buffer -- search results, project-replace's preview,
    // project-agenda -- visits whatever result is under point instead of
    // doing nothing (the buffer can't accept a literal newline anyway,
    // being read-only). VisitSearchResult's own silent no-op on a
    // non-matching line (see its doc comment) is what makes this safe to
    // key off ReadOnly() alone, without needing to know which specific
    // kind of results buffer this is.
    if (chord->Special == editor::SpecialKey::Enter && !chord->Control && !chord->Meta && activeBuffer_.Get().ReadOnly()) {
        VisitSearchResult();
        return true;
    }

    if (HandleConflictQuickKey(*chord)) {
        return true;
    }
    if (HandleMultibufferQuickKey(*chord)) {
        return true;
    }
    if (editor::vim::ModeEnabled()) {
        return HandleVimKey(*chord);
    }
    return DispatchChordNormally(*chord);
}

void BufferView::OnPaste(std::string_view text) {
    HandleBulkPastedText(text);
}

void BufferView::HandleBulkPastedText(std::string_view text) {
    if (text.empty()) {
        return;
    }

    const bool vimInsertOrOff =
        !editor::vim::ModeEnabled() || vimEngine_.CurrentMode() == editor::vim::Mode::Insert;
    if (inputMode_ == InputMode::Normal && vimInsertOrOff) {
        // Fast path: the actual fix -- one atomic InsertAtPoint call, one
        // ContentGeneration() bump, one undo step, regardless of text's
        // length. Same shape as the middle-click primary-selection paste
        // above (TakeFocus/ClearMark/ReadOnly-guarded InsertAtPoint).
        TakeFocus();
        text::Buffer& buffer = activeBuffer_.Get();
        buffer.ClearMark();
        if (!buffer.ReadOnly()) {
            buffer.InsertAtPoint(text);
            // Vim dot-repeat (".") bookkeeping: string-append only, no
            // buffer mutation/reparse per iteration -- so a paste landing
            // in vim Insert-mode still replays correctly afterward. '\n'/
            // '\t' are recorded as real Special::Enter/Tab chords (matching
            // exactly what RecordInsertKey sees for an actual keypress),
            // not as plain codepoints -- same reasoning as the slow path's
            // own NCKEY_ENTER re-encoding just below.
            if (editor::vim::ModeEnabled()) {
                std::size_t offset = 0;
                while (offset < text.size()) {
                    const std::size_t next      = text::NextCodepointBoundary(text, offset);
                    const char32_t    codepoint = text::DecodeCodepointUtf8(text, offset);
                    if (codepoint == U'\n') {
                        vimEngine_.RecordInsertKey(editor::KeyChord{.Special = editor::SpecialKey::Enter});
                    }
                    else if (codepoint == U'\t') {
                        vimEngine_.RecordInsertKey(editor::KeyChord{.Special = editor::SpecialKey::Tab});
                    }
                    else {
                        vimEngine_.RecordInsertKey(editor::KeyChord{.Codepoint = codepoint});
                    }
                    offset = next;
                }
            }
        }
        return; // viewport_.ScrollToShowPoint() already runs every Paint() -- no explicit call needed
    }

    // Slow path: any other modal InputMode (isearch, M-x, any prompt), or
    // vim mode active outside Insert -- replay each decoded codepoint
    // through the exact same per-character dispatch every ordinary
    // keystroke already goes through, so whichever mode is active handles
    // pasted text exactly as if it had been typed. A pasted '\n' is
    // re-encoded back to NCKEY_ENTER (matching Notcurses' own load_ncinput
    // normalization every real Enter keypress already goes through) so it
    // round-trips through DecodeBaseKey's Ctrl-fallback logic correctly
    // instead of misdecoding as Ctrl+J; a literal Tab needs no re-encoding
    // since NCKEY_TAB is already 0x09.
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t next      = text::NextCodepointBoundary(text, offset);
        const char32_t    codepoint = text::DecodeCodepointUtf8(text, offset);
        ncinput           ni{};
        ni.id     = (codepoint == U'\n') ? NCKEY_ENTER : static_cast<std::uint32_t>(codepoint);
        ni.evtype = NCTYPE_PRESS;
        OnKeyEvent(Event(ni));
        offset = next;
    }
}

bool BufferView::DispatchChordNormally(const editor::KeyChord& chord) {
    // No ClampPointToNarrowing() here either: RunCommandAndHandleOutcome
    // handles it internally now (see its own doc comment) -- required,
    // not just tidier, since a dispatched command can be split-window/
    // delete-window/other-window, which synchronously destroys *this*
    // BufferView via StartInteractiveSession's own window-forwarding path;
    // a clamp call made from out here, after the dispatch returns, would be
    // touching an already-destroyed object in exactly that case (a real,
    // SIGSEGV-confirmed bug caught by this session's own WindowManagerTest.cpp
    // suite, not a hypothetical one).
    // status-message-lifecycle follow-up: attemptedSequence reconstructs
    // exactly what Feed's own pending_ will see (its first line is
    // pending_.push_back(chord)) -- captured *before* calling Feed since
    // Feed clears pending_ itself on a NoMatch/Unbound result, so there'd be
    // nothing left to read afterward otherwise.
    std::vector<editor::KeyChord> attemptedSequence = dispatcher_.Pending();
    attemptedSequence.push_back(chord);

    editor::Dispatcher::Outcome outcome = editor::Dispatcher::Outcome::Unbound;
    editor::CommandContext      context = MakeContext();
    context.viewportHeight              = size().height > 0 ? static_cast<std::size_t>(size().height) : 0;
    // prefix-argument follow-up: hand this one dispatch attempt the pending
    // value and clear the member up front -- Dispatcher::Feed decides
    // whether it was actually consumed (Invoked/NoMatch) or must survive
    // (Pending, a multi-chord sequence still in progress), restored below in
    // the branch the comment beneath already establishes is safe to still
    // touch `this` in. An Invoked outcome can synchronously destroy *this*
    // (window-management commands), so nothing after that may write to a
    // member -- pre-clearing here means the Invoked/consumed case doesn't
    // need to.
    context.prefixArg = pendingPrefixArg_;
    pendingPrefixArg_.reset();
    // UAF follow-up: a copy, not a reference to the member -- the ran==true
    // branch below fires this *after* RunCommandAndHandleOutcome, and an
    // Invoked outcome can synchronously destroy *this* (window-management
    // commands like delete-window closing this very pane's BufferView).
    // Reading onPrefixHintChanged_ off `this` there was a genuine
    // heap-use-after-free, confirmed live under ASan
    // ("delete-window on the focused pane in a 2-window split focuses the
    // survivor" reproduces it deterministically) -- this local copy is safe
    // to call regardless of whether *this* still exists by then.
    const std::function<void(std::optional<WhichKeyHint>)> onPrefixHintChangedCopy = onPrefixHintChanged_;
    const bool                                             ran                     = RunCommandAndHandleOutcome(
        context,
        [&] {
            outcome = dispatcher_.Feed(chord, context);
            return outcome == editor::Dispatcher::Outcome::Invoked;
        },
        &chord);

    // ran (not outcome) gates this: outcome can be stale -- if Feed's own
    // Match case invokes a command that throws, Feed never reaches its
    // `return Outcome::Invoked` line, leaving outcome at its default
    // Unbound even though a real command genuinely ran (and already
    // reported its own exception message via RunCommandAndHandleOutcome's
    // catch). ran correctly reflects that either way. Only reached when
    // !ran (Pending/Unbound never invoke a command, so *this* is never at
    // risk of having been destroyed by a window-management
    // interactiveRequest here -- see RunCommandAndHandleOutcome's own doc
    // comment).
    if (!ran) {
        if (outcome == editor::Dispatcher::Outcome::Pending) {
            pendingPrefixArg_ = context.prefixArg;                                      // Feed leaves it untouched mid-sequence -- keep it alive
            statusMessage_    = editor::FormatKeySequence(dispatcher_.Pending()) + "-"; // matches real Emacs' own "C-x-" while-waiting convention
            if (onPrefixHintChanged_) {
                onPrefixHintChanged_(editor::WhichKeyEnabled() ? std::optional(BuildWhichKeyHint()) : std::nullopt);
            }
        }
        else if (outcome == editor::Dispatcher::Outcome::Unbound) {
            statusMessage_ = editor::FormatKeySequence(attemptedSequence) + " is undefined";
            if (onPrefixHintChanged_) {
                onPrefixHintChanged_(std::nullopt);
            }
        }
    }
    else if (onPrefixHintChangedCopy) {
        onPrefixHintChangedCopy(std::nullopt);
    }
    return true;
}

WhichKeyHint BufferView::BuildWhichKeyHint() const {
    WhichKeyHint hint;
    hint.prefixLabel = editor::FormatKeySequence(dispatcher_.Pending()) + "-"; // matches statusMessage_'s own convention above
    for (const auto& child : dispatcher_.Keymaps().ChildrenAt(dispatcher_.Pending())) {
        hint.bindings.emplace_back(editor::FormatKeyChord(child.chord), child.commandName.value_or("..."));
    }
    return hint;
}

bool BufferView::RunCommandAndHandleOutcome(editor::CommandContext& context, const std::function<bool()>& invoke,
                                            const editor::KeyChord* triggeringChord) {
    const std::size_t generationBefore    = activeBuffer_.Get().ContentGeneration();
    const std::size_t pointBefore         = activeBuffer_.Get().Point(); // documentHighlight follow-up: see MaybeScheduleDocumentHighlight's call site below
    const std::string statusMessageBefore = statusMessage_;              // status-message-lifecycle: see the "clear if unchanged" check below
    // Diff gutter markers follow-up: read alongside generationBefore, for
    // the same "safe as long as this dispatch doesn't itself switch active
    // buffers" reasoning MaybeScheduleAutoCompletion's own generationBefore
    // comparison already relies on -- see this method's tail below.
    const bool wasModifiedBefore = activeBuffer_.Get().Modified();
    // snippet-expansion follow-up (pre-dispatch hook): while a session is
    // live, every buffer-modifying path funnels through here
    // (HandleSnippetKey re-dispatches everything it doesn't consume), so
    // this is the one choke point that can wrap the dispatched command and
    // its mirror sync (post-dispatch hook below) into one undo group --
    // kill-line/yank/C-u-repeats inside a field get identical treatment to
    // plain typing. Buffer re-resolved by name on each side (the command
    // may close it), never a stored pointer; an armed pristine-placeholder
    // delete applies here so it shares the keystroke's own undo step.
    const bool snippetHookArmed = inputMode_ == InputMode::Snippet && snippetSession_.has_value();
    if (snippetHookArmed) {
        if (text::Buffer* snippetBuffer = ResolveSnippetBuffer()) {
            snippetBuffer->BeginUndoGroup();
            if (snippetPendingPristineDelete_) {
                snippetSession_->DeleteActiveFieldContent(*snippetBuffer);
                snippetSession_->ClearPristine();
            }
        }
        snippetPendingPristineDelete_ = false;
    }
    // linked-editing-range follow-up (pre-dispatch hook): same "wrap the
    // dispatched command and its own mirror sync into one undo group" shape
    // as the snippet hook just above, but armed purely on
    // linkedEditingSession_ itself rather than an InputMode -- this session
    // must keep mirroring during ordinary Normal-mode typing, not a distinct
    // mode a caller has to be in.
    const bool linkedEditingHookArmed = linkedEditingSession_.has_value();
    if (linkedEditingHookArmed) {
        if (text::Buffer* linkedBuffer = ResolveLinkedEditingBuffer()) {
            linkedBuffer->BeginUndoGroup();
        }
    }
    bool ran = false;
    try {
        ran = invoke();
    }
    catch (const std::exception& e) {
        ReportError(e.what());
        ran = true; // a command did run, it just threw -- still "something happened"
    }

    // status-message-lifecycle follow-up: a real, invoked command (ran ==
    // true -- Pending/Unbound never reach here with ran true, so a
    // still-accumulating prefix sequence's own about-to-be-shown "C-x-"
    // indicator is never touched by this) that didn't itself report
    // anything new clears whatever stale message was already showing --
    // "just sitting there" after some other real action (moving point,
    // editing, anything) doesn't make sense. Guarded on statusMessage_
    // still matching what it was before this dispatch even started: a
    // command that explicitly re-sets the exact same text (rare) is
    // indistinguishable from one that never touched it at all with this
    // diff-only approach -- a narrow, documented trade-off rather than
    // threading a "did I actually write something" flag through every one
    // of dozens of existing command implementations.
    if (ran && !statusMessage_.empty() && statusMessage_ == statusMessageBefore) {
        statusMessage_.clear();
    }

    // snippet-expansion follow-up (post-dispatch hook): sync mirrors and
    // close the pre-hook's undo group, then decide whether the session
    // survives this dispatch. Placed above the context.quit early return so
    // the group balances on every path, and above the interactiveRequest
    // block so a request that destroys this pane (delete-window) never
    // leaves orphaned ranges behind -- *this* is still alive here
    // regardless of what the command asked for (destruction only happens
    // inside StartInteractiveSession below).
    if (snippetHookArmed && snippetSession_) {
        text::Buffer* snippetBuffer = ResolveSnippetBuffer();
        if (snippetBuffer != nullptr) {
            snippetSession_->SyncMirrors(*snippetBuffer);
            snippetBuffer->EndUndoGroup();
        }
        if (snippetBuffer == nullptr                                             // buffer closed
            || !snippetSession_->RangesValid(*snippetBuffer)                     // undo/redo cleared the ranges
            || &activeBuffer_.Get() != snippetBuffer                             // command switched buffers
            || context.interactiveRequest != editor::InteractiveRequest::None) { // another session starting
            EndSnippetSession();
        }
        else if (statusMessage_.empty()) {
            statusMessage_ = snippetSession_->StatusText();
        }
    }

    // linked-editing-range follow-up (post-dispatch hook): same placement
    // reasoning as the snippet post-dispatch block above -- balances the
    // pre-hook's undo group on every path, and runs before the
    // interactiveRequest block so a pane-destroying request never leaves
    // orphaned ranges behind. C-g/Escape always ends the session outright
    // (IsQuit), the one exit this class's own PointStillInside/RangesValid
    // checks can't catch by themselves since neither moves point nor
    // touches undo.
    if (linkedEditingHookArmed && linkedEditingSession_) {
        text::Buffer* linkedBuffer = ResolveLinkedEditingBuffer();
        if (linkedBuffer != nullptr) {
            linkedEditingSession_->SyncMirrors(*linkedBuffer);
            linkedBuffer->EndUndoGroup();
        }
        if (linkedBuffer == nullptr || (triggeringChord && IsQuit(*triggeringChord)) ||
            !linkedEditingSession_->RangesValid(*linkedBuffer) || &activeBuffer_.Get() != linkedBuffer ||
            !linkedEditingSession_->PointStillInside(*linkedBuffer) ||
            context.interactiveRequest != editor::InteractiveRequest::None) {
            EndLinkedEditingSession();
        }
        else if (statusMessage_.empty()) {
            statusMessage_ = linkedEditingSession_->StatusText();
        }
    }

    if (context.quit) {
        // eventLoop_ is nullptr outside a real, running-editor SetEventLoop
        // call -- every unit test, and any other headless use of
        // BufferView. It is never null during real, running-editor usage
        // (main.cpp is what constructs the EventLoop and wires it in), but
        // the null check stays strict rather than being dropped as a
        // hypothetical risk -- skipping it crashed the whole process the
        // instant a test exercised `quit`, confirmed via a real SIGSEGV.
        // Shown by the final frame EventLoop::Run renders after Exit():
        // teardown (LSP child grace waits, session saves) runs after Run
        // returns but before the terminal is restored, and without this the
        // pause reads as a hang rather than deliberate cleanup.
        statusMessage_ = "Shutting down...";
        if (eventLoop_) {
            eventLoop_->Exit();
        }
        return ran;
    }

    // suspend-frame follow-up: same eventLoop_-null-check precedent as quit
    // above (unset in every unit test/headless use). Not exclusive with
    // anything else a dispatch might have done, so it falls through to the
    // rest of ordinary post-command handling below rather than returning
    // early -- EventLoop::Suspend() itself only flips a flag Run() consumes
    // once this whole dispatch has returned, so nothing here blocks.
    if (context.suspend && eventLoop_) {
        eventLoop_->Suspend();
    }

    // lsp-format-on-save follow-up: save-buffer/save-buffer-force set this
    // instead of running their own saveBufferBody synchronously when an LSP
    // round trip should format the buffer first -- the actual save hasn't
    // happened yet, so none of the normal post-command refresh below
    // applies until RequestLspFormatThenSaveBuffer's own callback runs it.
    if (context.deferSaveForLspFormat) {
        RequestLspFormatThenSaveBuffer();
        return ran;
    }

    // structural-selection-expansion follow-up: any dispatched command other
    // than expand-selection/shrink-selection themselves invalidates the
    // expansion-history stack -- this is the one choke point every dispatch
    // (typing, arrow motion, everything) passes through, so it's what
    // catches ordinary editing/motion, which never touches
    // interactiveRequest at all (stays InteractiveRequest::None). A
    // command-driven buffer switch (find-file, switch-to-buffer, ...) is
    // covered here too; a non-command-driven one (a TabBar/ProjectSidebar
    // mouse click) is instead caught by ExpandSelection/ShrinkSelection's own
    // buffer-identity staleness check in StartInteractiveSession.
    if (ran && context.interactiveRequest != editor::InteractiveRequest::ExpandSelection &&
        context.interactiveRequest != editor::InteractiveRequest::ShrinkSelection) {
        expansionHistory_.clear();
    }

    if (context.interactiveRequest != editor::InteractiveRequest::None) {
        // context is a reference to the *caller's* own local CommandContext
        // (never a member of this), so reading context.interactiveRequest
        // is always safe regardless of what StartInteractiveSession just
        // did -- but calling ClampPointToNarrowing() (which touches
        // activeBuffer_, a real member) afterward is not, for exactly the
        // IsWindowManagementRequest cases (see that function's own doc
        // comment). Skip it there; every other request is a normal,
        // still-alive-*this* interactive session (isearch, a prompt, ...).
        const bool destroysThisPane = IsWindowManagementRequest(context.interactiveRequest);
        // Emacs-keymap-round-2 follow-up: zap-to-char's own invocation is
        // the only place with real access to context.lastCommand (see
        // CommandContext::zapToCharAppend's own doc comment) -- stash its
        // decision here, before the character keystroke that actually
        // performs the kill (which bypasses Dispatcher::Feed, and so never
        // sees a meaningful lastCommand of its own) needs it.
        if (context.interactiveRequest == editor::InteractiveRequest::ZapToChar) {
            pendingZapToCharAppend_ = context.zapToCharAppend;
        }
        // snippet-expansion follow-up: same stash shape as zapToCharAppend
        // just above -- the requesting command's context is a caller local,
        // so what to expand has to ride a member into
        // StartInteractiveSession's own SnippetExpand case.
        if (context.interactiveRequest == editor::InteractiveRequest::SnippetExpand) {
            pendingSnippetExpansion_ = context.snippetExpansion;
        }
        StartInteractiveSession(context.interactiveRequest);
        if (!destroysThisPane) {
            ClampPointToNarrowing();
        }
        return ran;
    }

    // hover/completion follow-up: only reached for an ordinary, non-
    // interactive, still-alive-*this* dispatch -- exactly the "organic
    // keystroke" case worth considering for automatic completion.
    // triggeringChord is only non-null from OnKeyEvent's own call site (see
    // this method's own doc comment in BufferView.h), so macro replay/M-x
    // never reach this.
    if (triggeringChord) {
        MaybeScheduleAutoCompletion(*triggeringChord, generationBefore);
        MaybeScheduleSignatureHelp(*triggeringChord, generationBefore);
        MaybeScheduleOnTypeFormatting(*triggeringChord, generationBefore);
    }

    // Diff gutter markers follow-up: unlike MaybeScheduleAutoCompletion
    // above, not gated on triggeringChord/plain-self-insert -- deletions,
    // undo, paste, and format-on-save should all refresh the gutter too,
    // not just organic typing. A save (Modified() transitioning true ->
    // false) bypasses the debounce entirely and refreshes right away, the
    // same "this is a natural point to want it fresh" reasoning a real
    // save deserves; anything else just re-arms the debounce timer.
    if (wasModifiedBefore && !activeBuffer_.Get().Modified()) {
        RequestDiffForCurrentBuffer();
    }
    else if (activeBuffer_.Get().ContentGeneration() != generationBefore) {
        ScheduleDiffRefresh();
    }

    // Editable-multibuffer follow-up: an edit inside the *diagnostics*
    // multibuffer leaves its Diagnostic entries pointing at stale composite
    // bytes -- Diagnostic is deliberately not relocated (a real LSP server
    // re-publishes its full set after every change; a synthetic multibuffer
    // has no server to do that for it). Clearing loses the severity
    // coloring after the first edit, but never shows a diagnostic
    // underline pointing at the wrong bytes. Gated on ExcerptRanges() being
    // non-empty, so this is a no-op for every ordinary buffer -- and it's
    // deliberately not gated on any "this is specifically the diagnostics
    // buffer" marker: any multibuffer with both excerpt ranges and
    // diagnostics set is, today, only ever the one RequestDiagnosticsBuffer
    // builds.
    if (activeBuffer_.Get().ContentGeneration() != generationBefore && !activeBuffer_.Get().ExcerptRanges().empty() &&
        !activeBuffer_.Get().Diagnostics().empty()) {
        activeBuffer_.Get().SetDiagnostics({});
    }

    // documentHighlight follow-up: not gated on triggeringChord/plain-self-
    // insert -- arrow motion, search, and any other point-moving command
    // must refresh the highlighted-occurrences set too, not just organic
    // typing (the completion-popup precedent this otherwise mirrors).
    MaybeScheduleDocumentHighlight(pointBefore, generationBefore);

    // Debugging wishlist (line-inspect follow-up): dap-line-inspect is
    // on-demand, not live-refreshed like documentHighlight above -- this
    // just clears the stale highlight once the buffer switched, its content
    // changed, or point left the inspected line, rather than scheduling a
    // fresh request.
    if (lineInspect_ && (lineInspect_->buffer != &activeBuffer_.Get() ||
                         lineInspect_->contentGeneration != activeBuffer_.Get().ContentGeneration() ||
                         activeBuffer_.Get().Content().ByteOffsetToLine(activeBuffer_.Get().Point()) != lineInspect_->line)) {
        lineInspect_.reset();
    }

    ClampPointToNarrowing();
    // multi-cursor-round-2 follow-up: a command that just added a secondary
    // cursor (add-cursor-above/-below, select-next-occurrence) reports its
    // new offset here instead of moving point itself -- scroll to show that
    // cursor rather than the ordinary (unmoved) primary point.
    if (context.newlyAddedCursorPoint) {
        viewport_.ScrollToShowOffset(*context.newlyAddedCursorPoint);
    }
    else {
        viewport_.ScrollToShowPoint();
    }
    return ran;
}

void BufferView::EnsureStatusMessageFreshness() {
    // While any interactive session is active (isearch, a prompt like
    // "Project search: ", query-replace, ...), statusMessage_ is that
    // session's own live, actively-managed text -- e.g. what the user has
    // typed into a prompt so far. It must never be auto-cleared out from
    // under them just because they paused for a few seconds mid-typing,
    // and it's already re-shown on every keystroke by the session's own
    // Handle*Key method regardless, so there's nothing for the idle timer
    // to usefully guard here. Must also cancel any deadline already armed
    // before this session started (a Normal-mode message that was showing
    // when the session opened): statusMessageTimer_'s fire callback reads
    // statusMessage_/statusMessageSnapshot_ live rather than a value
    // captured at arm time, and the snapshot sync just below keeps them
    // equal throughout the session -- so an uncancelled old timer would
    // fire mid-session and find them "unchanged," wiping the session's own
    // live text (e.g. the isearch query) right out from under the user.
    if (inputMode_ != InputMode::Normal) {
        statusMessageSnapshot_ = statusMessage_;
        statusMessageChangedAt_.reset();
        statusMessageTimer_.Cancel(); // drop any deadline armed before this session started -- see the comment above
        return;
    }

    if (statusMessage_ == statusMessageSnapshot_) {
        return; // nothing wrote a new message since the last Paint() call
    }
    statusMessageSnapshot_ = statusMessage_;
    if (statusMessage_.empty()) {
        statusMessageChangedAt_.reset(); // nothing to time out
        return;
    }
    statusMessageChangedAt_ = std::chrono::steady_clock::now();

    // DeadlineTimer::Arm starts a real background thread that wakes exactly
    // once, kStatusMessageTimeout from now, and Posts the clear back onto
    // the loop thread -- no polling needed. Guarded by statusMessageSnapshot_
    // still matching statusMessage_ at fire time: if something else wrote a
    // new message before this deadline elapsed, this fire must not clear
    // text it didn't set (a fresh deadline for that newer text was armed
    // by this same method's own next call, which re-armed
    // statusMessageTimer_; Arm() cancels any not-yet-fired previous callback,
    // so only the latest deadline ever actually fires).
    if (eventLoop_) {
        statusMessageTimer_.Arm(*eventLoop_, kStatusMessageTimeout, [this] {
            if (statusMessage_ == statusMessageSnapshot_) {
                statusMessage_.clear();
                statusMessageSnapshot_.clear();
            }
            statusMessageChangedAt_.reset();
        });
    }
}

void BufferView::ReportError(std::string message, editor::LogCategory category) {
    editor::LogMessage(category, editor::LogSeverity::Error, message);
    statusMessage_ = std::move(message);
}

void BufferView::RequestQuickFixAtPoint() {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = codeActionRequest_.Begin();

    // Same diagnostic-at-point range/server-routing preference as
    // RequestCodeActionsAtPoint -- see that method's own doc comment.
    std::size_t rangeStart = point;
    std::size_t rangeEnd   = point;
    // embedded-language-documents follow-up: see RequestCodeActionsAtPoint's
    // identical comment above.
    std::string serverKey = ResolvedLspServerKey(point);
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

    statusMessage_ = "Requesting quick fix...";
    lspManager_->RequestCodeActions(
        buffer, rangeStart, rangeEnd,
        [this, bufferPtr, point, generation, serverKey](std::vector<editor::lsp::CodeAction> actions) {
            if (codeActionRequest_.IsStale(generation)) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent
            }
            codeActionServerKey_ = serverKey;
            if (actions.empty()) {
                statusMessage_ = "No quick fix available.";
                return;
            }
            // Pick without asking only when the choice is unambiguous: a
            // lone action, else a lone isPreferred one (the server's own
            // "this is the auto-fix" marker), else a lone quickfix-kind one.
            // Anything murkier falls back to the ordinary selection list --
            // silently applying one of several plausible fixes would be
            // worse than one extra keystroke.
            const editor::lsp::CodeAction* pick = nullptr;
            if (actions.size() == 1) {
                pick = &actions[0];
            }
            for (const auto selector : {+[](const editor::lsp::CodeAction& a) { return a.isPreferred; },
                                        +[](const editor::lsp::CodeAction& a) { return a.kind.rfind("quickfix", 0) == 0; }}) {
                if (pick != nullptr) {
                    break;
                }
                const editor::lsp::CodeAction* sole = nullptr;
                for (const editor::lsp::CodeAction& action : actions) {
                    if (!selector(action)) {
                        continue;
                    }
                    if (sole != nullptr) {
                        sole = nullptr; // more than one match -- ambiguous, try the next selector
                        break;
                    }
                    sole = &action;
                }
                pick = sole;
            }
            if (pick != nullptr) {
                ResolveAndApplyCodeAction(*pick);
                return;
            }
            pendingCodeActions_  = std::move(actions);
            codeActionSelection_ = 0;
            inputMode_           = InputMode::LspCodeActionSelect;
            RefreshCodeActionSelectStatus();
        },
        serverKey);
}

void BufferView::ReplayMacro() {
    if (replayingMacro_) {
        return;
    }

    const std::vector<editor::KeyChord> macro = dispatcher_.LastMacro();
    if (macro.empty()) {
        statusMessage_ = "No keyboard macro has been recorded yet.";
        return;
    }

    replayingMacro_ = true;
    for (const editor::KeyChord& chord : macro) {
        // snippet-macro-replay follow-up: a chord the live session would
        // consume itself (Tab/S-Tab field navigation, ESC, a pristine-
        // placeholder Backspace) gets the identical treatment here --
        // HandleSnippetNavigationKey never invokes an arbitrary command, so
        // *this* can't have been destroyed by it and there's nothing to
        // dispatch afterward. Anything it doesn't consume (plain typing,
        // kill-line, ...) falls through to the same Feed-with-a-locally-
        // owned-context dispatch every other replayed chord already uses --
        // required, not just simpler, since that's also what makes a
        // window-management command reachable this way (delete-window is
        // bound globally, still resolvable from inside a snippet field)
        // safe to detect without ever touching *this* afterward.
        if (inputMode_ == InputMode::Snippet && HandleSnippetNavigationKey(chord)) {
            // handled entirely above -- fall to the trailing inputMode_
            // check below like any other iteration.
        }
        else {
            editor::CommandContext context = MakeContext();
            context.viewportHeight         = size().height > 0 ? static_cast<std::size_t>(size().height) : 0;
            RunCommandAndHandleOutcome(context, [&] { return dispatcher_.Feed(chord, context) == editor::Dispatcher::Outcome::Invoked; });

            // context.quit/interactiveRequest are the caller's own local
            // copies (see RunCommandAndHandleOutcome's own doc comment),
            // always safe to read even if *this* was just destroyed --
            // checked first and exclusively in that case. A recorded macro
            // can perfectly well contain a delete-window/split/other-window
            // step -- IsWindowManagementRequest alone gets the immediate
            // `return` (touching inputMode_/replayingMacro_ below would
            // otherwise be a real, if narrow, use-after-free -- this bug
            // predates the snippet-replay branch above, found and fixed
            // alongside it, not introduced by it); context.quit doesn't
            // destroy the pane (eventLoop_->Exit() already happened inside
            // RunCommandAndHandleOutcome regardless of this check), so it's
            // still safe to fall through to replayingMacro_ = false below.
            if (IsWindowManagementRequest(context.interactiveRequest)) {
                return;
            }
            if (context.quit) {
                break;
            }
        }
        if (inputMode_ != InputMode::Normal && inputMode_ != InputMode::Snippet) {
            break;
        }
    }
    replayingMacro_ = false;
}

void BufferView::HandleQueryReplaceKeyInner(const editor::KeyChord& chord) {
    const auto stage = queryReplace_->CurrentStage();

    if (stage == editor::QueryReplace::Stage::EnteringPattern || stage == editor::QueryReplace::Stage::EnteringReplacement) {
        if (chord.Special == editor::SpecialKey::Enter) {
            if (stage == editor::QueryReplace::Stage::EnteringPattern) {
                try {
                    queryReplace_->ConfirmPattern();
                }
                catch (const editor::RegexPatternError& e) {
                    ReportError(std::string("Invalid regex: ") + e.what());
                    return; // stays in EnteringPattern; don't overwrite the message below
                }
            }
            else {
                queryReplace_->ConfirmReplacement();
            }
        }
        else if (IsQuit(chord)) {
            queryReplace_->Cancel();
        }
        else if (chord.Special == editor::SpecialKey::Backspace) {
            queryReplace_->DeleteChar();
        }
        else if (IsPlainCharacter(chord)) {
            queryReplace_->AppendChar(chord.Codepoint);
        }
    }
    else if (stage == editor::QueryReplace::Stage::Confirming) {
        if (chord.Codepoint == U'y') {
            queryReplace_->ReplaceAndNext();
        }
        else if (chord.Codepoint == U'n') {
            queryReplace_->SkipAndNext();
        }
        else if (chord.Codepoint == U'!') {
            queryReplace_->ReplaceAll();
        }
        else if (chord.Codepoint == U'q' || IsQuit(chord)) {
            queryReplace_->Finish();
        }
    }

    statusMessage_ = queryReplace_->StatusText();
    return;
}

void BufferView::RequestDiagnosticsBuffer() {
    // One entry per Code-origin diagnostic across every open, path-backed
    // buffer -- prose/spell-check diagnostics (Editor/Lsp/Manager.h's
    // kProseLanguageKey) stay out, matching the recent split that moved
    // them out of the code-diagnostic gutter entirely (they get their own
    // review flow, not this one). source is a raw pointer into
    // bufferList_'s own storage, valid for this whole synchronous function
    // (no callback/await in between, unlike RequestVcsFullDiffBuffer).
    struct PendingDiagnostic {
        text::Buffer*            source;
        text::Buffer::Diagnostic diagnostic;
        std::size_t              sourceLineStart;
    };
    std::vector<PendingDiagnostic> pending;
    for (const auto& bufferPtr : bufferList_.Buffers()) {
        text::Buffer& buffer = *bufferPtr;
        if (!buffer.Path() || buffer.Diagnostics().empty() || editor::multibuffer::MultibufferIndexFor(buffer)) {
            continue;
        }
        const text::ITextStorage& content = buffer.Content();
        for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
            if (diagnostic.origin != text::Buffer::Diagnostic::Origin::Code) {
                continue;
            }
            const std::size_t line = content.ByteOffsetToLine(std::min(diagnostic.startByte, content.ByteLength()));
            pending.push_back(PendingDiagnostic{&buffer, diagnostic, content.LineToByteOffset(line)});
        }
    }

    // Grouped per file, top-to-bottom within a file -- a scannable
    // "problems list," not whatever order each language server happened to
    // report diagnostics in.
    std::sort(pending.begin(), pending.end(), [](const PendingDiagnostic& a, const PendingDiagnostic& b) {
        const std::filesystem::path& pathA = *a.source->Path();
        const std::filesystem::path& pathB = *b.source->Path();
        return pathA != pathB ? pathA < pathB : a.diagnostic.startByte < b.diagnostic.startByte;
    });

    // One excerpt per diagnostic -- its own single source line, verbatim
    // (not a multi-line context window: the header already names the exact
    // file/line, and this keeps the composite-offset translation below a
    // fixed "past the header" arithmetic instead of needing real line
    // math against the composite buffer).
    std::vector<editor::multibuffer::ExcerptSource> excerpts;
    excerpts.reserve(pending.size());
    for (const PendingDiagnostic& item : pending) {
        const text::ITextStorage& content = item.source->Content();
        const std::size_t         line    = content.ByteOffsetToLine(item.sourceLineStart);
        const std::size_t         lineEnd = (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        // U+25B8, ProjectSidebar's own disclosure triangle -- same header
        // glyph vcs-full-diff-buffer's excerpts use, for visual consistency
        // between the two multibuffer consumers.
        std::string header = "▸ " + item.source->Path()->string() + ":" + std::to_string(line + 1);
        std::string body   = content.Substring(item.sourceLineStart, lineEnd - item.sourceLineStart);
        excerpts.push_back(editor::multibuffer::ExcerptSource{
            *item.source->Path(), line + 1, line + 1, std::move(header), std::move(body), {}, /*editable=*/true});
    }

    text::Buffer& results = editor::multibuffer::BuildMultibuffer(bufferList_, "*diagnostics*", excerpts);
    editor::SetLastResultsBuffer("*diagnostics*");
    activeBuffer_.Set(results);
    statusMessage_ =
        excerpts.empty() ? "No diagnostics." : std::to_string(excerpts.size()) + " diagnostic" + (excerpts.size() == 1 ? "" : "s");

    // Re-attaches each original diagnostic's real severity/message onto the
    // composite buffer, translated into composite byte space, instead of a
    // new LineTint -- see this method's own doc comment in BufferView.h for
    // why: it makes the ordinary diagnostic gutter glyph/underline/
    // severity-color/inline-annotation pipeline (Theme::diagnosticError et
    // al., all driven off Buffer::Diagnostics()) light up unmodified. Every
    // excerpt above is exactly one header line + one body line, so the
    // body's own start byte is a fixed offset (header length + its
    // newline) past the excerpt's span start. ExcerptSpan order matches
    // excerpts' own order 1:1 here: BuildMultibuffer appends excerpts
    // sequentially so composite bytes only increase, and
    // MultibufferIndex::SetSpans's sort-by-compositeStartByte is a no-op on
    // an already-increasing sequence.
    if (editor::multibuffer::MultibufferIndex* index = editor::multibuffer::MultibufferIndexFor(results)) {
        const std::vector<editor::multibuffer::ExcerptSpan>& spans = index->Spans();
        std::vector<text::Buffer::Diagnostic>                composited;
        composited.reserve(pending.size());
        for (std::size_t i = 0; i < pending.size() && i < spans.size(); ++i) {
            const editor::multibuffer::ExcerptSpan& span      = spans[i];
            const std::size_t                       bodyLen   = excerpts[i].bodyText.size();
            const std::size_t                       bodyStart = span.compositeStartByte + excerpts[i].headerText.size() + 1;

            const text::Buffer::Diagnostic& original   = pending[i].diagnostic;
            std::size_t                     startDelta = original.startByte > pending[i].sourceLineStart ? original.startByte - pending[i].sourceLineStart : 0;
            startDelta                                 = std::min(startDelta, bodyLen);
            std::size_t endDelta                       = original.endByte > pending[i].sourceLineStart ? original.endByte - pending[i].sourceLineStart : startDelta;
            endDelta                                   = std::min(endDelta, bodyLen);
            if (endDelta <= startDelta) {
                endDelta = std::min(bodyLen, startDelta + 1); // widen a zero-length span, same as the inline-diagnostic underline pass
            }

            text::Buffer::Diagnostic translated = original;
            translated.startByte                = bodyStart + startDelta;
            translated.endByte                  = bodyStart + endDelta;
            composited.push_back(translated);
        }
        results.SetDiagnostics(std::move(composited));
    }
}

void BufferView::StageOrUnstageFileAtPoint(bool stage) {
    if (!vcsRunner_) {
        statusMessage_ = "no vcs runner configured";
        return;
    }
    const std::optional<std::filesystem::path> target = ResolveVcsFileTarget();
    if (!target) {
        statusMessage_ = "no file to " + std::string(stage ? "stage" : "unstage") + " here";
        return;
    }

    auto onSuccess = [this, stage, target = *target] {
        statusMessage_ = (stage ? "Staged " : "Unstaged ") + target.filename().string();
        RefreshVcsStatusBuffer();
        // Staging moves a file's changes into the index, which the bundled
        // git plugin's worktree-vs-index diff then stops reporting --
        // refresh so the gutter agrees (and the reverse for unstaging).
        RequestDiffForCurrentBuffer();
    };
    auto onError = [this, stage](std::string error) {
        statusMessage_ = std::string("vcs ") + (stage ? "stage" : "unstage") + ": " + error;
    };
    if (stage) {
        vcsRunner_->RequestStage(*target, std::move(onSuccess), std::move(onError));
    }
    else {
        vcsRunner_->RequestUnstage(*target, std::move(onSuccess), std::move(onError));
    }
}

void BufferView::NextErrorForTesting() {
    NextError();
}

void BufferView::PreviousErrorForTesting() {
    PreviousError();
}

void BufferView::RequestDiagnosticsBufferForTesting() {
    RequestDiagnosticsBuffer();
}

void BufferView::DispatchStatusForTesting(std::vector<editor::vcs::StatusEntry> entries) {
    BuildVcsStatusBuffer(entries, /*announce=*/true);
}

void BufferView::DispatchBranchesForTesting(std::vector<editor::vcs::BranchEntry> entries) {
    BuildVcsBranchesBuffer(entries);
}

void BufferView::SendResultLineToAgent() {
    const std::string bufferName = activeBuffer_.Get().Name();
    if (bufferName != editor::MessagesBufferName() && bufferName != editor::testrun::TestResultsBufferName()) {
        statusMessage_ = "Not on a diagnostic/test-result line.";
        return;
    }
    const std::optional<ResultLineLocation> loc = ResultLineAtPoint();
    if (!loc) {
        statusMessage_ = "Not on a diagnostic/test-result line.";
        return;
    }

    // A handful of lines either side of the failing line -- enough for the
    // agent to see the surrounding function/statement without dumping the
    // whole file.
    constexpr std::size_t kContextLines = 5;
    const std::size_t     startLine     = (loc->lineNumber > kContextLines) ? (loc->lineNumber - kContextLines) : 1;
    const std::size_t     endLine       = loc->lineNumber + kContextLines;

    // Prefer an already-open buffer's live content (in case it has unsaved
    // edits) over a disk read -- ReadFileLines' own "path need not be an
    // open Buffer" fallback covers everything else.
    std::string excerpt;
    if (text::Buffer* openBuffer = bufferList_.FindByPath(loc->path)) {
        const text::ITextStorage& openContent = openBuffer->Content();
        const std::size_t         lineCount   = openContent.LineCount();
        const std::size_t         clampedEnd  = std::min(endLine, lineCount);
        for (std::size_t lineNumber = startLine; lineNumber <= clampedEnd; ++lineNumber) {
            const std::size_t zeroIndexed = lineNumber - 1;
            const std::size_t lineStart   = openContent.LineToByteOffset(zeroIndexed);
            const std::size_t lineEnd =
                (zeroIndexed + 1 < lineCount) ? openContent.LineToByteOffset(zeroIndexed + 1) - 1 : openContent.ByteLength();
            excerpt += "  " + std::to_string(lineNumber) + ": " + openContent.Substring(lineStart, lineEnd - lineStart) + "\n";
        }
    }
    else {
        excerpt = ReadFileLines(loc->path, startLine, endLine);
    }

    const editor::acp::Manager::PromptAttachment attachment{
        .uri      = "file://" + std::filesystem::absolute(loc->path).string(),
        .name     = loc->path.string() + ":" + std::to_string(loc->lineNumber),
        .mimeType = "",
        .text     = excerpt.empty() ? "(source unavailable)" : excerpt,
    };

    const std::string prompt = "The following diagnostic/test failure needs help:\n\n" + loc->fullLineText +
                               "\n\nPlease help me understand and fix this.";
    statusMessage_           = acpManager_->SendPrompt(prompt, {attachment});
}

bool BufferView::TestRunPreconditionsMet() {
    if (!testRunner_) {
        statusMessage_ = "No test runner available.";
        return false;
    }
    if (!mode_.testDiscovery) {
        statusMessage_ = "No test discovery configured for " + mode_.name + ".";
        return false;
    }
    if (!editor::testrun::TestFilterCommand()) {
        statusMessage_ = "No test filter command configured (see ned/set-test-filter-command).";
        return false;
    }
    return true;
}

void BufferView::RunSingleTest(const std::string& testName) {
    if (!testRunner_) {
        return; // callers check TestRunPreconditionsMet first; belt and braces
    }
    const text::Buffer& buffer = activeBuffer_.Get();
    const std::string   file   = buffer.Path() ? buffer.Path()->string() : std::string();
    if (text::Buffer* output = testRunner_->RunFiltered(testName, file)) {
        activeBuffer_.Set(*output);
    }
    statusMessage_ = "Running \"" + testName + "\"...";
}

void BufferView::RequestCloseBuffer(text::Buffer& buffer) {
    if (inputMode_ != InputMode::Normal) {
        statusMessage_ = "Finish the current prompt first.";
        return;
    }

    if (!buffer.Modified() || buffer.ReadOnly()) {
        CloseBufferNow(buffer);
        return;
    }

    pendingClose_  = &buffer;
    inputMode_     = InputMode::ConfirmCloseBuffer;
    statusMessage_ = "Buffer \"" + buffer.Name() + "\" has unsaved changes; close anyway? (y/n)";
}

void BufferView::BeginConfirmOpenBinary(const std::filesystem::path& path) {
    pendingBinaryOpenPath_ = path;
    inputMode_             = InputMode::ConfirmOpenBinary;
    statusMessage_         = "\"" + path.string() + "\" looks like a binary file; open anyway? (y/n)";
}

void BufferView::CloseBufferNow(text::Buffer& buffer) {
    const bool        wasActive = (&activeBuffer_.Get() == &buffer);
    const std::string name      = buffer.Name();

    text::Buffer* replacement = nullptr;
    if (wasActive) {
        // MRU-close follow-up: land on the buffer the user most recently
        // left, not the first tab -- the ActiveBuffer on-change hook (see
        // Pane's constructor) keeps this order current. Falls back to
        // list order for buffers never activated (e.g. session-restored
        // and never visited).
        replacement = bufferList_.MostRecentlyUsedBuffer(&buffer);
        if (replacement == nullptr) {
            for (const auto& candidate : bufferList_.Buffers()) {
                if (candidate.get() != &buffer) {
                    replacement = candidate.get();
                    break;
                }
            }
        }
        // Closing the only remaining buffer would leave nothing to edit --
        // Emacs itself never lets a frame end up with zero buffers either,
        // it just conjures a fresh *scratch* -- so do the same here rather
        // than refusing outright.
        if (replacement == nullptr) {
            replacement = &bufferList_.CreateBuffer("scratch");
        }
    }

    // Window-splitting follow-up: fired before the actual erase, while
    // `buffer` is still genuinely alive -- so a multi-pane owner can
    // retarget any *other* pane whose own ActiveBuffer also pointed at it.
    // This BufferView's own activeBuffer_ is already handled below,
    // independently of this hook.
    if (onBufferClosed_) {
        onBufferClosed_(buffer);
    }

    bufferList_.Close(name);
    if (wasActive && replacement != nullptr) {
        activeBuffer_.Set(*replacement);
    }
    statusMessage_.clear();
}

void BufferView::CloseEligibleBuffers(const std::vector<text::Buffer*>& targets) {
    std::size_t closed  = 0;
    std::size_t skipped = 0;
    for (text::Buffer* target : targets) {
        if (target->Modified() && !target->ReadOnly()) {
            ++skipped;
            continue;
        }
        CloseBufferNow(*target);
        ++closed;
    }
    if (skipped > 0) {
        statusMessage_ = "Closed " + std::to_string(closed) + " tab(s); " + std::to_string(skipped) +
                         " left open (unsaved changes).";
    }
    else if (closed > 0) {
        statusMessage_ = "Closed " + std::to_string(closed) + " tab(s).";
    }
    else {
        statusMessage_ = "No other tabs to close.";
    }
}

void BufferView::CloseOtherTabs(text::Buffer& keep) {
    std::vector<text::Buffer*> targets;
    for (const auto& candidate : bufferList_.Buffers()) {
        if (candidate.get() != &keep) {
            targets.push_back(candidate.get());
        }
    }
    CloseEligibleBuffers(targets);
}

void BufferView::CloseTabsToTheRight(text::Buffer& from) {
    const auto&                buffers = bufferList_.Buffers();
    const auto                 it      = std::find_if(buffers.begin(), buffers.end(),
                                                      [&](const std::unique_ptr<text::Buffer>& candidate) { return candidate.get() == &from; });
    std::vector<text::Buffer*> targets;
    if (it != buffers.end()) {
        for (auto rest = std::next(it); rest != buffers.end(); ++rest) {
            targets.push_back(rest->get());
        }
    }
    CloseEligibleBuffers(targets);
}

void BufferView::SetScrollBar(ScrollBar* scrollBar) {
    scrollBar_ = scrollBar;
}

void BufferView::SetScrollArrows(ScrollArrowButton* up, ScrollArrowButton* down) {
    scrollUpArrow_   = up;
    scrollDownArrow_ = down;
}

void BufferView::SetProjectSidebar(ProjectSidebar* sidebar) {
    projectSidebar_ = sidebar;
}

void BufferView::SetLeftDock(LeftDock* dock) {
    leftDock_ = dock;
}

void BufferView::SetMinimap(Minimap* minimap, Widget* scrollColumn) {
    minimap_             = minimap;
    minimapScrollColumn_ = scrollColumn;
}

void BufferView::SetTaskRunner(editor::tasks::TaskRunner* taskRunner) {
    taskRunner_ = taskRunner;
}

void BufferView::SetProjectUndo(editor::ProjectUndoManager* projectUndo) {
    projectUndo_ = projectUndo;
}

void BufferView::SetTestRunner(editor::testrun::TestRunner* testRunner) {
    testRunner_ = testRunner;
}

void BufferView::SetSurfaceUnseenLogEntries(bool enabled) {
    surfaceUnseenLogEntries_ = enabled;
}

void BufferView::SetJanetEnvironment(const janet::Environment* janetEnv) {
    janetEnv_ = janetEnv;
}

void BufferView::SetEventLoop(EventLoop* eventLoop) {
    eventLoop_ = eventLoop;
}

} // namespace ned::ui
