//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// The LSP feature broker: completion, hover, signature help, code actions/lens,
// definition/peek, symbols, hierarchy, rename, linked editing, and jump history.
//

#include "UI/BufferView/Internal.h"

namespace ned::ui {

// The file-local helpers these definitions call live in BufferView/Internal.h
// now that several parts share them -- see that header. This using-directive is
// what let the split leave every call site untouched.
using namespace detail;

void BufferView::SetOnCompletionChanged(std::function<void(std::optional<ListPopupModel>)> handler) {
    onCompletionChanged_ = std::move(handler);
}

void BufferView::SetOnHoverChanged(std::function<void(std::optional<ListPopupModel>)> handler) {
    onHoverChanged_ = std::move(handler);
}

std::string BufferView::ResolvedLspServerKey(std::size_t byteOffset) {
    EnsureEmbeddedDocumentCache();
    text::Buffer& buffer = activeBuffer_.Get();
    const auto    it     = embeddedDocumentCacheByBuffer_.find(&buffer);
    if (it == embeddedDocumentCacheByBuffer_.end()) {
        return {};
    }
    if (const std::optional<std::string> language = editor::EmbeddedLanguageAtByteOffset(it->second.documents, byteOffset)) {
        return *language;
    }
    return {};
}

void BufferView::RequestCompletionAtPoint(const std::string& triggerCharacter) {
    // Completion is a Normal-mode-only construct (see OnKeyEvent's
    // activeCompletion_ block): a debounce timer armed by Normal-mode typing
    // can fire after an interactive session has since started -- found live
    // with the snippet session, where typing a trigger word armed the timer
    // and TAB's expansion won the race, leaving a completion popped over the
    // active field that the session's own TAB could then never accept.
    // MaybeScheduleAutoCompletion's scheduling-side gate can't cover an
    // already-armed timer, so the fire path bails here too.
    if (inputMode_ != InputMode::Normal) {
        return;
    }
    text::Buffer&     buffer = activeBuffer_.Get();
    const std::size_t point  = buffer.Point();

    // embedded-language-documents follow-up: an empty serverKey means point
    // isn't inside an embedded region -- use the host language's own key,
    // same as before this feature existed. A non-empty one routes to that
    // language's own server (e.g. "javascript" inside an HTML <script>
    // block) for both the status check below and the request itself.
    const std::string serverKey   = ResolvedLspServerKey(point);
    const std::string languageKey = serverKey.empty() ? editor::LanguageKeyForMode(mode_) : serverKey;

    // dabbrev-fallback follow-up: StatusForLanguage never spawns a client,
    // so this is a pure "is one currently usable" check -- NotConfigured/
    // SpawnFailed/Disconnected all fall back to scanning the buffer itself
    // rather than asking a server that isn't there.
    const bool hasRunningLsp = lspManager_ && lspManager_->StatusForLanguage(lspManager_->ConnectionKeyForBuffer(
                                                  buffer, languageKey)) == editor::lsp::LspManager::LspStatus::Running;
    if (!hasRunningLsp) {
        // Self-hosting-completion follow-up: tried ahead of plain
        // dabbrev-expand for a Janet-mode buffer, falling through to it when
        // there's no janetEnv_ wired or nothing fuzzy-matches (e.g. a local
        // variable name rather than a "ned/*" binding).
        if (languageKey == "janet" && ApplyJanetBindingCompletion(buffer, point)) {
            return;
        }
        ApplyDabbrevCompletion(buffer, point);
        return;
    }

    text::Buffer* const bufferPtr   = &buffer;
    const std::size_t   generation  = completionRequest_.Begin();
    const std::size_t   prefixStart = WordPrefixStart(buffer.Content(), point);

    lspManager_->RequestCompletion(
        buffer, point,
        [this, bufferPtr, point, prefixStart, generation](editor::lsp::CompletionList list) {
            if (completionRequest_.IsStale(generation)) {
                return; // superseded by a newer request
            }
            // bufferPtr is only ever compared, never dereferenced, unless
            // this comparison already confirms it's the (guaranteed alive)
            // current active buffer -- safe even if the buffer it pointed to
            // was since closed, the same idiom BufferList::PreviewBuffer's
            // own mutable Buffer* already relies on.
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent
            }
            if (list.items.empty()) {
                activeCompletion_.reset();
                NotifyCompletionChanged();
                return;
            }
            completionPrefixRule_ = CompletionPrefixRule::Word;
            activeCompletion_.emplace(std::move(list.items), list.isIncomplete, activeBuffer_.Get().Content(), point, prefixStart);
            if (activeCompletion_->Empty()) {
                // Every item the server sent was filtered out against the
                // prefix already typed (CompletionSession ranks on
                // construction, not just on narrowing) -- an empty popup is
                // no popup.
                activeCompletion_.reset();
            }
            NotifyCompletionChanged();
        },
        serverKey, triggerCharacter);
}

std::vector<std::string> BufferView::CompletionTriggerCharacters() {
    // completion-trigger-characters follow-up: this replaced a hardcoded
    // ". : >" whose own comment noted that completionProvider.
    // triggerCharacters "was never plumbed". The fallback below IS that
    // hardcoded set, kept for exactly two cases it's still the best answer
    // for: no LSP server at all (the dabbrev path, where nothing declares
    // anything), and a server that advertises completion without naming any
    // trigger characters. Member access "." plus C++ scope resolution "::"
    // and arrow "->" -- both of the latter matched on their final character,
    // which is why ":" and ">" are listed rather than the two-character
    // sequences themselves.
    static const std::vector<std::string> kFallback = {".", ":", ">"};
    if (!lspManager_) {
        return kFallback;
    }
    const text::Buffer&                                      buffer      = activeBuffer_.Get();
    const std::string                                        serverKey   = ResolvedLspServerKey(buffer.Point());
    const std::string                                        languageKey = serverKey.empty() ? editor::LanguageKeyForMode(mode_) : serverKey;
    const std::optional<editor::lsp::CompletionProviderInfo> provider =
        lspManager_->CompletionProviderFor(lspManager_->ConnectionKeyForBuffer(buffer, languageKey));
    if (!provider || provider->triggerCharacters.empty()) {
        return kFallback;
    }
    return provider->triggerCharacters;
}

void BufferView::MaybeScheduleCompletionResolve() {
    if (!activeCompletion_ || !lspManager_ || !eventLoop_) {
        return;
    }
    const std::vector<editor::CompletionCandidate>& candidates = activeCompletion_->Candidates();
    const std::size_t                               selected   = activeCompletion_->SelectedIndex();
    if (selected >= candidates.size() || candidates[selected].resolved || candidates[selected].item.raw.is_null()) {
        return; // already answered for, or a synthesized item with nothing to send back
    }
    text::Buffer&                                            buffer      = activeBuffer_.Get();
    const std::string                                        serverKey   = ResolvedLspServerKey(buffer.Point());
    const std::string                                        languageKey = serverKey.empty() ? editor::LanguageKeyForMode(mode_) : serverKey;
    const std::optional<editor::lsp::CompletionProviderInfo> provider =
        lspManager_->CompletionProviderFor(lspManager_->ConnectionKeyForBuffer(buffer, languageKey));
    if (!provider || !provider->resolveProvider) {
        return; // this server never advertised completionItem/resolve -- see ResolveCompletionItem's own doc comment
    }
    completionResolveRequest_.Cancel();
    completionResolveDebounceTimer_.Arm(*eventLoop_, std::chrono::milliseconds(editor::lsp::LspCompletionDebounceMs()),
                                        [this] { RequestCompletionResolve(); });
}

void BufferView::RequestCompletionResolve() {
    if (!activeCompletion_ || !lspManager_ || inputMode_ != InputMode::Normal) {
        return;
    }
    const std::size_t selected = activeCompletion_->SelectedIndex();
    if (selected >= activeCompletion_->Candidates().size() || activeCompletion_->Candidates()[selected].resolved) {
        return; // the selection moved (or was answered for) between arming and firing
    }
    // Copied, not referenced: ResolveCompletionItem's callback runs after a
    // full round trip, by which point Refilter may have rebuilt the very
    // vector this candidate lives in.
    const editor::lsp::CompletionItem item       = activeCompletion_->Candidates()[selected].item;
    text::Buffer&                     buffer     = activeBuffer_.Get();
    text::Buffer* const               bufferPtr  = &buffer;
    const std::string                 serverKey  = ResolvedLspServerKey(buffer.Point());
    const std::size_t                 generation = completionResolveRequest_.Current();
    lspManager_->ResolveCompletionItem(
        buffer, item,
        [this, bufferPtr, generation, selected, label = item.label](std::optional<editor::lsp::CompletionItem> resolved) {
            if (completionResolveRequest_.IsStale(generation) || !activeCompletion_ || bufferPtr != &activeBuffer_.Get()) {
                return; // superseded, dismissed, or the buffer changed under us
            }
            if (!resolved) {
                return;
            }
            // The generation guard already rules out a *newer* selection,
            // but not a list narrowed under this one by a keystroke that
            // left the selection index numerically valid -- comparing the
            // label is what confirms row `selected` is still the row this
            // was requested for. ApplyResolution's own bounds check covers
            // the rest.
            const std::vector<editor::CompletionCandidate>& candidates = activeCompletion_->Candidates();
            if (selected >= candidates.size() || candidates[selected].item.label != label) {
                return;
            }
            activeCompletion_->ApplyResolution(selected, *resolved);
            NotifyCompletionChanged();
        },
        serverKey);
}

void BufferView::ApplyDabbrevCompletion(text::Buffer& buffer, std::size_t point) {
    const text::ITextStorage& content     = buffer.Content();
    const std::size_t         prefixStart = WordPrefixStart(content, point);
    const std::string         prefix      = content.Substring(prefixStart, point - prefixStart);

    std::vector<std::string> words = editor::CollectDabbrevCandidates(buffer.Text(), point, prefix);
    if (words.empty()) {
        activeCompletion_.reset();
        NotifyCompletionChanged();
        return;
    }
    std::vector<editor::lsp::CompletionItem> items;
    items.reserve(words.size());
    for (std::string& word : words) {
        // completion-popup follow-up: kind 1 == LSP CompletionItemKind::Text
        // -- a buffer-scanned word has no real semantic category, but a
        // fixed, sensible glyph beats the popup's "unrecognized kind"
        // fallback for every dabbrev row.
        items.push_back(editor::lsp::CompletionItem{.label = word, .insertText = word, .kind = 1});
    }
    completionPrefixRule_ = CompletionPrefixRule::Word;
    activeCompletion_.emplace(std::move(items), /*isIncomplete=*/false, content, point, prefixStart);
    NotifyCompletionChanged();
}

bool BufferView::ApplyJanetBindingCompletion(text::Buffer& buffer, std::size_t point) {
    if (!janetEnv_) {
        return false;
    }

    const std::string text        = buffer.Text();
    const std::size_t prefixStart = editor::JanetSymbolPrefixStart(text, point);
    const std::string prefix      = text.substr(prefixStart, point - prefixStart);
    if (prefix.empty()) {
        return false;
    }

    const std::vector<std::string> names  = janetEnv_->BindingNamesWithPrefix("ned/");
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(names, prefix);

    std::vector<editor::lsp::CompletionItem> items;
    items.reserve(ranked.size());
    for (const std::string& name : ranked) {
        // A subsequence match can never be shorter than the query it matched
        // against, so name.size() == prefix.size() here only when name IS
        // prefix verbatim -- point already sits right after a complete
        // binding name, nothing left to suggest (DabbrevComplete.h's own
        // "exact-length matches excluded" rule, applied here for the same
        // reason: CompletionInsertSuffix's insertText-doesn't-share-prefix
        // fallback would otherwise show the whole name again as a bogus
        // duplicate suffix).
        if (name.size() == prefix.size()) {
            continue;
        }
        // completion-popup follow-up: kind 3 == LSP CompletionItemKind::
        // Function -- every "ned/*" binding is, semantically, a callable.
        items.push_back(editor::lsp::CompletionItem{.label = name, .insertText = name, .kind = 3});
    }
    if (items.empty()) {
        return false;
    }
    completionPrefixRule_ = CompletionPrefixRule::JanetSymbol;
    activeCompletion_.emplace(std::move(items), /*isIncomplete=*/false, buffer.Content(), point, prefixStart);
    NotifyCompletionChanged();
    return true;
}

bool BufferView::ShouldSuppressAutoCompletion() const {
    const text::Buffer& buffer = activeBuffer_.Get();
    const std::size_t   point  = buffer.Point();
    if (point == 0) {
        return false;
    }
    const text::ITextStorage& content = buffer.Content();

    if (highlightCacheStamp_.IsFor(&buffer)) {
        const std::size_t                        line        = content.ByteOffsetToLine(point);
        const std::size_t                        lineStart   = content.LineToByteOffset(line);
        const std::size_t                        lineEnd     = (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        const std::vector<editor::HighlightSpan> lineSpans   = SpansForLine(highlightCacheSpans_, lineStart, lineEnd);
        const std::size_t                        priorOffset = content.PreviousCodepointBoundary(point);
        switch (SpanAtOffset(lineSpans, priorOffset).syntaxClass) {
            case editor::SyntaxClass::String:
            case editor::SyntaxClass::StringEscape:
            case editor::SyntaxClass::Comment:
            case editor::SyntaxClass::DocComment:
            case editor::SyntaxClass::Number:
                return true;
            default:
                break;
        }
    }

    // Fallback, independent of highlighting availability (covers
    // FundamentalMode and any other mode with no highlighter): a purely
    // numeric token immediately before point shouldn't trigger completion
    // either -- the exact "typing a number" complaint that motivated this
    // heuristic. WordPrefixStart's own ASCII-only guarantee makes token a
    // safe raw-byte string to scan.
    const std::size_t prefixStart = WordPrefixStart(content, point);
    if (prefixStart < point) {
        const std::string token = content.Substring(prefixStart, point - prefixStart);
        if (std::all_of(token.begin(), token.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
            return true;
        }
    }
    return false;
}

std::size_t BufferView::CurrentCompletionPrefixStart(const text::Buffer& buffer, std::size_t point) const {
    if (completionPrefixRule_ == CompletionPrefixRule::JanetSymbol) {
        return editor::JanetSymbolPrefixStart(buffer.Text(), point);
    }
    return WordPrefixStart(buffer.Content(), point);
}

void BufferView::MaybeScheduleAutoCompletion(const editor::KeyChord& chord, std::size_t generationBefore) {
    text::Buffer& buffer         = activeBuffer_.Get();
    const bool    contentChanged = buffer.ContentGeneration() != generationBefore;

    // completion-fidelity follow-up: the incremental-narrowing path. A live
    // session gets first refusal on the keystroke, before any of the
    // new-request gates below -- those decide whether to *start* a request,
    // which is a different question from whether an existing candidate set
    // still applies. This is what removed a full round trip (and a popup
    // blink) from every character typed while completing.
    //
    // Deliberately ahead of the Special/modifier gate too: Backspace never
    // schedules a request, but it does widen a live session.
    if (activeCompletion_ && contentChanged) {
        const std::size_t point = buffer.Point();
        switch (activeCompletion_->Refilter(buffer.Content(), point, CurrentCompletionPrefixStart(buffer, point))) {
            case editor::CompletionSession::Outcome::Keep:
                NotifyCompletionChanged(); // narrowed in place, no request at all
                return;
            case editor::CompletionSession::Outcome::Rerequest:
                // Keep showing the locally-narrowed list while the fresh
                // request is in flight -- RequestCompletionAtPoint only
                // replaces activeCompletion_ once a response actually
                // lands, so the popup never blinks out for the round trip.
                NotifyCompletionChanged();
                break;
            case editor::CompletionSession::Outcome::Dismiss:
                activeCompletion_.reset();
                NotifyCompletionChanged();
                break;
        }
    }
    else if (activeCompletion_) {
        return; // a keystroke that changed nothing leaves the popup exactly as it was
    }
    // snippet-expansion follow-up: completion is a Normal-mode-only
    // construct (see OnKeyEvent's activeCompletion_ block), but typing
    // inside a snippet field reaches here through HandleSnippetKey's
    // re-dispatch -- without this gate the debounce timer could pop a
    // suggestion mid-session that TAB (consumed by the session) could then
    // never accept.
    if (inputMode_ == InputMode::Snippet) {
        return;
    }
    // dabbrev-fallback follow-up: no longer gated on lspManager_ being set at
    // all -- RequestCompletionAtPoint (fired once this debounce elapses)
    // itself decides between an LSP request and the buffer-word fallback.
    // The auto-popup toggle still governs both sources uniformly.
    if (!editor::lsp::LspAutoCompleteEnabled()) {
        return;
    }
    if (chord.Control || chord.Meta || chord.Special != editor::SpecialKey::None) {
        return; // only plain self-insert keystrokes schedule automatic completion
    }
    // completion-auto-trigger-gate follow-up: only a word-continuation
    // keystroke (an identifier the user is actively typing) or a real
    // completion trigger character schedules a request -- everything else
    // (";", ")", "}", ",", whitespace, ...) is a statement/expression
    // boundary, not a place completions are useful. Found live: typing ";"
    // was popping the completion popup because clangd (like most servers)
    // happily answers textDocument/completion with general in-scope symbols
    // even for an empty/non-identifier prefix -- nothing here previously
    // looked at *which* character was typed, only the syntax class already
    // at point.
    //
    // completion-trigger-characters follow-up: that trigger set is now the
    // running server's own completionProvider.triggerCharacters, falling
    // back to the hardcoded ". : >" this shipped with -- see
    // CompletionTriggerCharacters. A trigger-character keystroke is also
    // reported as such to the server (triggerKind 2), which is a different
    // question a server is entitled to answer differently: a request caused
    // by "." should return members, not every in-scope symbol.
    std::string triggerCharacter;
    if (!IsWordCodepoint(chord.Codepoint)) {
        const std::string              typed    = text::EncodeCodepointUtf8(chord.Codepoint);
        const std::vector<std::string> triggers = CompletionTriggerCharacters();
        if (std::find(triggers.begin(), triggers.end(), typed) == triggers.end()) {
            return;
        }
        triggerCharacter = typed;
    }
    if (!contentChanged) {
        return; // nothing actually changed
    }
    if (ShouldSuppressAutoCompletion()) {
        return;
    }
    const std::chrono::milliseconds delay(editor::lsp::LspCompletionDebounceMs());
    completionDebounceDeadline_ = std::chrono::steady_clock::now() + delay;
    // DeadlineTimer::Arm fires exactly once, delay from now -- re-typing
    // before it fires re-arms it via this same call site on the very next
    // qualifying keystroke, cancelling the stale one outright, which is what
    // makes this a debounce rather than a fixed-interval repeat.
    if (eventLoop_) {
        completionDebounceTimer_.Arm(*eventLoop_, delay, [this, triggerCharacter = std::move(triggerCharacter)] {
            completionDebounceDeadline_.reset();
            RequestCompletionAtPoint(triggerCharacter);
        });
    }
}

void BufferView::MaybeScheduleDocumentHighlight(std::size_t pointBefore, std::size_t generationBefore) {
    text::Buffer& buffer = activeBuffer_.Get();
    if (buffer.Point() == pointBefore && buffer.ContentGeneration() == generationBefore) {
        return; // nothing moved -- no reason to re-request
    }
    if (documentHighlight_ && (documentHighlight_->buffer != &buffer || documentHighlight_->contentGeneration != buffer.ContentGeneration())) {
        documentHighlight_.reset(); // stale: buffer switched under us, or content changed since the last response
    }
    const std::chrono::milliseconds delay(editor::lsp::LspCompletionDebounceMs());
    if (eventLoop_) {
        documentHighlightDebounceTimer_.Arm(*eventLoop_, delay, [this] { RequestDocumentHighlightAtPoint(); });
    }
}

void BufferView::RequestDocumentHighlightAtPoint() {
    if (inputMode_ != InputMode::Normal) {
        documentHighlight_.reset();
        return;
    }
    text::Buffer&     buffer = activeBuffer_.Get();
    const std::size_t point  = buffer.Point();

    const std::string serverKey     = ResolvedLspServerKey(point);
    const std::string languageKey   = serverKey.empty() ? editor::LanguageKeyForMode(mode_) : serverKey;
    const bool        hasRunningLsp = lspManager_ && lspManager_->StatusForLanguage(lspManager_->ConnectionKeyForBuffer(
                                                         buffer, languageKey)) == editor::lsp::LspManager::LspStatus::Running;
    if (!hasRunningLsp) {
        documentHighlight_.reset();
        return;
    }

    text::Buffer* const bufferPtr                  = &buffer;
    const std::size_t   generation                 = documentHighlightRequest_.Begin();
    const std::size_t   contentGenerationAtRequest = buffer.ContentGeneration();
    lspManager_->RequestDocumentHighlight(
        buffer, point,
        [this, bufferPtr, point, generation, contentGenerationAtRequest](std::vector<editor::lsp::DocumentHighlight> highlights) {
            if (documentHighlightRequest_.IsStale(generation)) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point ||
                activeBuffer_.Get().ContentGeneration() != contentGenerationAtRequest) {
                return; // buffer/point/content changed since the request was sent
            }
            if (highlights.empty()) {
                documentHighlight_.reset();
                return;
            }
            const text::ITextStorage&                        content = bufferPtr->Content();
            std::vector<std::pair<std::size_t, std::size_t>> ranges;
            ranges.reserve(highlights.size());
            for (const editor::lsp::DocumentHighlight& highlight : highlights) {
                ranges.emplace_back(editor::lsp::LspPositionToByte(content, highlight.start),
                                    editor::lsp::LspPositionToByte(content, highlight.end));
            }
            documentHighlight_ = DocumentHighlightState{
                .buffer = bufferPtr, .contentGeneration = contentGenerationAtRequest, .requestPoint = point, .ranges = std::move(ranges)};
        },
        serverKey);
}

// hover-tooltips follow-up. See Widget.cpp's Event::mouse() and
// ROADMAP.md's Mouse Ergonomics section for the confirmed mechanism: this
// installed Notcurses build's own SGR decoder hard-codes a bare
// no-button-held motion report's evtype to NCTYPE_RELEASE (its own
// in.c comment: "oddly enough"), so such a move arrives here as
// MouseEvent{button = None, motion = Released} -- never Motion::Moved. That
// combination is otherwise unreachable (a real button release always
// carries a real button id), so it's the reliable "hovering, nothing held"
// signal on this backend.

void BufferView::MaybeScheduleHover(Point localMousePoint) {
    if (!lspManager_ || !onHoverChanged_ || !eventLoop_) {
        return;
    }
    if (!editor::lsp::LspHoverOnMouseMoveEnabled()) {
        return; // ned/set-lsp-hover-on-mouse-move disabled this -- no debounce armed, no request ever sent
    }
    const std::size_t gutterWidth = GutterWidth();
    if (localMousePoint.x < 0 || static_cast<std::size_t>(localMousePoint.x) <= gutterWidth) {
        DismissHover(); // hovering the gutter, not real text -- nothing meaningful to show
        return;
    }
    const std::size_t offset = viewport_.ByteOffsetForPoint(localMousePoint);
    if (hoverOffset_ == offset) {
        return; // already pending/shown for this exact spot -- nothing to do
    }
    hoverDebounceTimer_.Cancel();
    if (hoverOffset_) {
        // Moved off the previous spot -- hide it now rather than leaving a
        // stale tooltip floating over wherever the mouse used to be until
        // the new debounce settles.
        onHoverChanged_(std::nullopt);
    }
    hoverOffset_                               = offset;
    const std::size_t               generation = hoverRequest_.Begin();
    const Box&                      box        = Box_();
    const Point                     anchor{.x = box.x_min + localMousePoint.x, .y = box.y_min + localMousePoint.y + 1};
    const std::chrono::milliseconds delay(editor::lsp::LspCompletionDebounceMs());
    hoverDebounceTimer_.Arm(*eventLoop_, delay, [this, offset, anchor, generation] {
        RequestHoverAtOffset(offset, anchor, generation);
    });
}

void BufferView::RequestHoverAtOffset(std::size_t byteOffset, Point screenAnchor, std::size_t generation) {
    if (hoverRequest_.IsStale(generation) || !lspManager_) {
        return; // superseded by a newer hover, or the manager disappeared while this was pending
    }
    text::Buffer&       buffer    = activeBuffer_.Get();
    text::Buffer* const bufferPtr = &buffer;
    const std::string   serverKey = ResolvedLspServerKey(byteOffset);
    lspManager_->RequestHover(
        buffer, byteOffset,
        [this, bufferPtr, generation, screenAnchor](std::optional<std::string> text) {
            if (hoverRequest_.IsStale(generation) || bufferPtr != &activeBuffer_.Get()) {
                return; // superseded by a newer hover, or the buffer switched under us
            }
            if (!onHoverChanged_) {
                return;
            }
            if (!text || text->empty()) {
                onHoverChanged_(std::nullopt);
                return;
            }
            ListPopupModel model;
            model.anchor      = screenAnchor;
            model.previewText = *text;
            onHoverChanged_(std::move(model));
        },
        serverKey);
}

void BufferView::DismissHover() {
    hoverDebounceTimer_.Cancel();
    if (!hoverOffset_) {
        return;
    }
    hoverOffset_.reset();
    hoverRequest_.Cancel();
    if (onHoverChanged_) {
        onHoverChanged_(std::nullopt);
    }
}

void BufferView::RequestLinkedEditingRangeAtPoint() {
    if (snippetSession_) {
        // Both sessions use Buffer::SnippetRanges_ as their storage -- see
        // linkedEditingSession_'s own doc comment for why they can never
        // coexist.
        statusMessage_ = "Cannot start linked editing during an active snippet session.";
        return;
    }
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer                     = activeBuffer_.Get();
    text::Buffer* const bufferPtr                  = &buffer;
    const std::size_t   point                      = buffer.Point();
    const std::size_t   generation                 = linkedEditingRequest_.Begin();
    const std::string   serverKey                  = ResolvedLspServerKey(point);
    const std::size_t   contentGenerationAtRequest = buffer.ContentGeneration();

    lspManager_->RequestLinkedEditingRange(
        buffer, point,
        [this, bufferPtr, point, generation, contentGenerationAtRequest](std::vector<editor::lsp::LinkedEditingRange> ranges) {
            if (linkedEditingRequest_.IsStale(generation)) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point ||
                activeBuffer_.Get().ContentGeneration() != contentGenerationAtRequest || snippetSession_) {
                return; // buffer/point/content changed, or a snippet session started meanwhile
            }
            if (ranges.empty()) {
                statusMessage_ = "No linked ranges at point.";
                return;
            }
            const text::ITextStorage&                        content = bufferPtr->Content();
            std::vector<std::pair<std::size_t, std::size_t>> byteRanges;
            byteRanges.reserve(ranges.size());
            for (const editor::lsp::LinkedEditingRange& range : ranges) {
                byteRanges.emplace_back(editor::lsp::LspPositionToByte(content, range.start),
                                        editor::lsp::LspPositionToByte(content, range.end));
            }
            auto session = editor::LinkedEditingSession::Start(*bufferPtr, bufferPtr->Name(), byteRanges);
            if (!session) {
                statusMessage_ = "No linked ranges at point.";
                return;
            }
            linkedEditingSession_ = std::move(session);
            statusMessage_        = linkedEditingSession_->StatusText();
        },
        serverKey);
}

text::Buffer* BufferView::ResolveLinkedEditingBuffer() {
    if (!linkedEditingSession_) {
        return nullptr;
    }
    if (text::Buffer* buffer = bufferList_.Find(linkedEditingSession_->BufferName())) {
        return buffer;
    }
    if (activeBuffer_.Get().Name() == linkedEditingSession_->BufferName()) {
        return &activeBuffer_.Get();
    }
    return nullptr;
}

void BufferView::EndLinkedEditingSession() {
    if (linkedEditingSession_) {
        if (text::Buffer* buffer = ResolveLinkedEditingBuffer()) {
            linkedEditingSession_->Finish(*buffer);
        }
        linkedEditingSession_.reset();
    }
}

void BufferView::MaybeScheduleSignatureHelp(const editor::KeyChord& chord, std::size_t generationBefore) {
    // Deliberately not gated on InputMode::Snippet the way
    // MaybeScheduleAutoCompletion is -- typing "(" or "," while filling a
    // snippet tabstop argument is exactly when signature help is most
    // useful, and unlike the completion popup it never competes with TAB.
    if (inputMode_ != InputMode::Normal && inputMode_ != InputMode::Snippet) {
        return;
    }
    if (!editor::lsp::LspSignatureHelpAutoTriggerEnabled()) {
        return;
    }
    if (chord.Control || chord.Meta || chord.Special != editor::SpecialKey::None) {
        return;
    }
    if (chord.Codepoint != U'(' && chord.Codepoint != U',') {
        return; // only these two trigger characters schedule automatic signature help
    }
    if (activeBuffer_.Get().ContentGeneration() == generationBefore) {
        return; // nothing actually changed
    }
    const std::chrono::milliseconds delay(editor::lsp::LspCompletionDebounceMs());
    if (eventLoop_) {
        signatureHelpDebounceTimer_.Arm(*eventLoop_, delay, [this] { RequestSignatureHelpAtPoint(); });
    }
}

void BufferView::RequestSignatureHelpAtPoint() {
    if (inputMode_ != InputMode::Normal && inputMode_ != InputMode::Snippet) {
        return;
    }
    text::Buffer&     buffer = activeBuffer_.Get();
    const std::size_t point  = buffer.Point();

    const std::string serverKey   = ResolvedLspServerKey(point);
    const std::string languageKey = serverKey.empty() ? editor::LanguageKeyForMode(mode_) : serverKey;
    if (!lspManager_ || lspManager_->StatusForLanguage(lspManager_->ConnectionKeyForBuffer(buffer, languageKey)) !=
                            editor::lsp::LspManager::LspStatus::Running) {
        return;
    }

    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   generation = signatureHelpRequest_.Begin();
    lspManager_->RequestSignatureHelp(
        buffer, point,
        [this, bufferPtr, point, generation](std::optional<std::string> text) {
            if (signatureHelpRequest_.IsStale(generation)) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent
            }
            if (text) {
                statusMessage_ = *text; // EnsureStatusMessageFreshness() handles the auto-clear/timeout
            }
        },
        serverKey);
}

void BufferView::AcceptActiveCompletion() {
    if (!activeCompletion_) {
        return;
    }
    text::Buffer& buffer = activeBuffer_.Get();
    // completion-fidelity follow-up: one resolution rule for every source
    // and every item -- replace [replaceStart, point) with newText, where
    // replaceStart came from the server's own textEdit if it sent one and
    // from the caller's word-boundary rule otherwise. This replaced a
    // prefix-subtraction scheme whose fallback (insertText doesn't start
    // with the typed prefix -- a server doing its own fuzzy matching, which
    // clangd and rust-analyzer both do) inserted the whole string *after*
    // the typed text, corrupting the line.
    const std::optional<editor::CompletionSession::AcceptPlan> plan = activeCompletion_->PlanAccept(buffer.Point());
    if (!plan) {
        return;
    }
    activeCompletion_.reset();
    NotifyCompletionChanged();

    // completion-additional-edits follow-up: the server's own
    // additionalTextEdits -- the "#include <vector>" an accepted std::vector
    // needs, the "import foo" a Python symbol needs. Applied *before* the
    // main insertion, not after, for two reasons: they were computed against
    // the document as it was before this item was inserted (so their
    // positions are only honest against that state), and applying them first
    // means the main range only has to be relocated once, by
    // ApplyWorkspaceTextEditsAndRelocate's own return value, rather than
    // every additional edit having to be re-resolved against a document the
    // insertion just moved.
    //
    // Deliberately NOT routed through ApplyProjectEdit despite ROADMAP's own
    // framing: per spec these always target the completed document itself,
    // so there is no second buffer for a multi-file transaction to hold, and
    // going through it would open a separate undo group (and record a
    // pointless one-file project transaction) rather than joining the
    // accept's own single step.
    const bool  hasAdditionalEdits = !plan->additionalEdits.empty();
    std::size_t replaceStart       = plan->replaceStart;
    std::size_t replaceEnd         = plan->replaceEnd;
    if (hasAdditionalEdits) {
        // Nestable, so this and the accept's own group below collapse into
        // one undo step: an undo that removed the inserted symbol but left
        // its "#include" behind would be a broken intermediate state, the
        // same reasoning the delete+insert pair is already grouped for.
        buffer.BeginUndoGroup();
        const std::size_t shiftedStart = editor::lsp::ApplyWorkspaceTextEditsAndRelocate(buffer, plan->additionalEdits, replaceStart);
        replaceEnd                     = (replaceEnd - replaceStart) + shiftedStart;
        replaceStart                   = shiftedStart;
    }
    const auto endAdditionalEditGroup = [&buffer, hasAdditionalEdits] {
        if (hasAdditionalEdits) {
            buffer.EndUndoGroup();
        }
    };

    if (plan->isSnippet) {
        // snippet-expansion follow-up: a snippet-format item's insertText
        // is TextMate syntax, never literal text -- replace the range with
        // the parsed expansion and start a tabstop session, the exact
        // InteractiveRequest::SnippetExpand path. BeginSnippetExpansion
        // already takes a range and does its own undo grouping.
        BeginSnippetExpansion(replaceStart, replaceEnd, plan->newText);
        endAdditionalEditGroup();
        return;
    }
    if (replaceStart == replaceEnd && plan->newText.empty()) {
        endAdditionalEditGroup();
        return; // nothing to insert -- but any additional edits already applied still stand
    }
    // One undo step for the delete+insert pair: accepting a completion is a
    // single user action, and an undo that left the typed prefix deleted
    // but the completion not inserted would be a broken intermediate state.
    buffer.BeginUndoGroup();
    if (replaceStart < replaceEnd) {
        // DeleteRange takes a *length*, not an end offset. Point sits at
        // replaceEnd, and DeleteRange relocates it to replaceStart, which is
        // exactly where InsertAtPoint below then writes.
        buffer.DeleteRange(replaceStart, replaceEnd - replaceStart);
    }
    if (!plan->newText.empty()) {
        buffer.InsertAtPoint(plan->newText);
    }
    buffer.EndUndoGroup();
    endAdditionalEditGroup();
}

void BufferView::AcceptActiveCompletionAt(std::size_t index) {
    if (!activeCompletion_) {
        return;
    }
    // completion-popup-scroll follow-up: `index` is a raw popup row, which
    // since the rows became windowed is no longer the same thing as an index
    // into the candidate list -- it's offset by the "N more above" divider
    // and relative to the window's own start. Same inverse mapping the
    // prompt pickers' own click path uses; a click on a divider row (or past
    // the end, a stale click racing a just-narrowed list) resolves to
    // nullopt and is ignored rather than accepting a neighbor.
    const std::optional<std::size_t> resolved =
        ResolveFuzzyCandidateRowIndex(index, activeCompletion_->SelectedIndex(), activeCompletion_->Candidates().size());
    if (!resolved) {
        return;
    }
    activeCompletion_->Select(*resolved);
    AcceptActiveCompletion();
}

void BufferView::ScrollCompletionPopup(int steps) {
    // completion-popup-scroll follow-up: the completion popup has no scroll
    // offset independent of its selection -- the window is derived from the
    // selected row (see NotifyCompletionChanged) -- so a wheel step *is* a
    // selection step, which also keeps wheel and Up/Down agreeing about what
    // Tab would accept. ScrollCandidatePopup's own shape, minus the
    // InputMode switch: completion is Normal-mode-only.
    if (steps == 0 || !activeCompletion_) {
        return;
    }
    const int direction = steps > 0 ? 1 : -1;
    const int count     = steps > 0 ? steps : -steps;
    for (int i = 0; i < count; ++i) {
        activeCompletion_->Cycle(direction);
    }
    NotifyCompletionChanged();
}

void BufferView::CycleActiveCompletion(int direction) {
    if (!activeCompletion_) {
        return;
    }
    activeCompletion_->Cycle(direction);
    NotifyCompletionChanged();
}

std::optional<Point> BufferView::CompletionAnchorNow() const {
    const std::optional<Point> local = CursorPosition();
    if (!local) {
        return std::nullopt;
    }
    // Same local-to-absolute conversion main.cpp's own render() callback
    // uses to place the real terminal cursor -- one row below point, so the
    // popup opens under the text being typed rather than over it.
    const Box& box = Box_();
    return Point{.x = box.x_min + local->x, .y = box.y_min + local->y + 1};
}

void BufferView::NotifyCompletionChanged() {
    if (!onCompletionChanged_) {
        return;
    }
    if (!activeCompletion_) {
        lastNotifiedCompletionAnchor_.reset();
        onCompletionChanged_(std::nullopt);
        return;
    }

    const std::optional<Point> anchor = CompletionAnchorNow();
    lastNotifiedCompletionAnchor_     = anchor;
    if (!anchor) {
        onCompletionChanged_(std::nullopt); // point's row isn't on screen this frame
        return;
    }

    const std::vector<editor::CompletionCandidate>& candidates = activeCompletion_->Candidates();
    if (candidates.empty()) {
        onCompletionChanged_(std::nullopt);
        return;
    }

    // completion-popup-scroll follow-up: the rows are windowed around the
    // selection, exactly the way BuildFuzzyCandidatePopupModel already does
    // it for every prompt picker -- ListPopup::Paint truncates at the box
    // height and always starts at row 0, so an unwindowed push of every
    // candidate scrolled the selection off the bottom invisibly once the
    // list passed the popup's own height. Reuses ComputeCandidatePopupWindow
    // so a click's own inverse mapping (ResolveFuzzyCandidateRowIndex in
    // AcceptActiveCompletionAt) can reproduce the identical window without
    // a second copy of the math.
    const auto [windowStart, windowEnd] = ComputeCandidatePopupWindow(activeCompletion_->SelectedIndex(), candidates.size());

    ListPopupModel model;
    model.anchor = anchor;
    model.rows.reserve((windowEnd - windowStart) + 2);
    if (windowStart > 0) {
        model.rows.push_back({.main = "↑ " + std::to_string(windowStart) + " more above"});
    }
    model.selectedIndex = (activeCompletion_->SelectedIndex() - windowStart) + (windowStart > 0 ? 1 : 0);
    for (std::size_t i = windowStart; i < windowEnd; ++i) {
        const editor::lsp::CompletionItem& item = candidates[i].item;
        ListPopupRow                       row;
        row.main  = item.label;
        row.right = item.detail;
        if (const std::optional<editor::SymbolKind> bucket = CompletionKindBucket(item.kind)) {
            row.left           = SymbolGlyphFor(*bucket);
            row.leftForeground = theme_.BrushFor(editor::SyntaxClassFor(*bucket)).foreground;
        }
        else {
            // Unrecognized/absent kind (a bare keyword, snippet, file path,
            // ... -- see CompletionKindBucket's own doc comment): a dim,
            // generic marker rather than no glyph at all, so every row
            // still has a visual anchor in this column.
            row.left           = "·"; // MIDDLE DOT
            row.leftForeground = theme_.ghostTextForeground;
        }
        model.rows.push_back(std::move(row));
    }
    if (const std::size_t hiddenBelow = candidates.size() - windowEnd; hiddenBelow > 0) {
        model.rows.push_back({.main = "↓ " + std::to_string(hiddenBelow) + " more below"});
    }
    // completion-popup-preview follow-up: the *selected* item's own
    // documentation, not every item's -- ListPopup renders it as a footer
    // below the row list, updated for free on every selection change since
    // this method already runs then (CycleActiveCompletion, a click, ...).
    if (const std::string& documentation = candidates[activeCompletion_->SelectedIndex()].item.documentation;
        !documentation.empty()) {
        model.previewText = documentation;
    }
    onCompletionChanged_(std::move(model));
    // completion-resolve follow-up: every selection change funnels through
    // here (cycling, a click, a wheel step, a narrowing keystroke), which
    // makes this the one place a per-item resolve has to be armed from --
    // and arming after the popup was already handed over means the fetched
    // documentation arrives as a second, later repaint rather than delaying
    // the first one behind a round trip.
    MaybeScheduleCompletionResolve();
}

void BufferView::RequestCodeActionsAtPoint() {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = codeActionRequest_.Begin();

    // Prefer the diagnostic covering point (same lookup lsp-show-diagnostic
    // already does), else a zero-length range at point. executeCommand/
    // prose-code-actions follow-up: a Prose-origin diagnostic at point
    // routes the whole request to the prose checker connection instead of
    // the primary language server -- that server has no notion of a
    // harper-ls-flagged word, only harper-ls's own connection does.
    std::size_t rangeStart = point;
    std::size_t rangeEnd   = point;
    // embedded-language-documents follow-up: defaults to point's own
    // embedded server (e.g. "javascript" inside an HTML <script> block), ""
    // meaning the host language -- RequestCodeActions's own default -- when
    // point isn't inside one. A Prose-origin diagnostic at point still wins
    // below, unchanged priority.
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

    statusMessage_ = "Requesting code actions...";
    lspManager_->RequestCodeActions(
        buffer, rangeStart, rangeEnd,
        [this, bufferPtr, point, generation, serverKey](std::vector<editor::lsp::CodeAction> actions) {
            if (codeActionRequest_.IsStale(generation)) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent -- see RequestCompletionAtPoint's own identical guard
            }
            pendingCodeActions_  = std::move(actions);
            codeActionServerKey_ = serverKey;
            if (pendingCodeActions_.empty()) {
                statusMessage_ = "No code actions available.";
                return;
            }
            codeActionSelection_ = 0;
            if (pendingCodeActions_.size() == 1) {
                ResolveAndApplyCodeAction(pendingCodeActions_[0]);
                return;
            }
            inputMode_ = InputMode::LspCodeActionSelect;
            RefreshCodeActionSelectStatus();
        },
        serverKey);
}

void BufferView::RequestCodeLensAtPoint() {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&             buffer    = activeBuffer_.Get();
    const text::ITextStorage& content   = buffer.Content();
    const std::size_t         point     = buffer.Point();
    const std::size_t         line      = content.ByteOffsetToLine(point);
    const std::size_t         lineStart = content.LineToByteOffset(line);
    const std::size_t         lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();

    const editor::lsp::LspManager::ResolvedCodeLens* found = nullptr;
    for (const auto& lens : lspManager_->CodeLensSpans(buffer)) {
        const bool onThisLine =
            (lens.startByte >= lineStart && lens.startByte < lineEnd) || (lineStart == lineEnd && lens.startByte == lineStart);
        if (onThisLine) {
            found = &lens; // v1: only the first lens on the line -- see this method's own doc comment in BufferView.h
            break;
        }
    }
    if (!found) {
        statusMessage_ = "No code lens at point.";
        return;
    }

    const std::string   serverKey = editor::LanguageKeyForMode(mode_);
    text::Buffer* const bufferPtr = &buffer;

    if (found->hasCommand) {
        statusMessage_ = "Running " + (found->title.empty() ? found->commandName : found->title) + "...";
        lspManager_->ExecuteCommand(buffer, serverKey, found->commandName, found->commandArguments,
                                    [this](bool ok) { statusMessage_ = ok ? "Code lens command executed." : "Code lens command failed."; });
        return;
    }

    // Copied, not a held pointer -- ResolveCodeLens's own callback runs
    // later, by which time a fresh RequestCodeLenses response (this
    // buffer's own per-Paint() background sync) could have replaced
    // CodeLensSpans' underlying vector out from under a raw pointer into it.
    const editor::lsp::LspManager::ResolvedCodeLens lensCopy = *found;
    statusMessage_                                           = "Resolving code lens...";
    lspManager_->ResolveCodeLens(
        buffer, lensCopy,
        [this, bufferPtr, serverKey](std::optional<editor::lsp::LspManager::ResolvedCodeLens> resolved) {
            if (bufferPtr != &activeBuffer_.Get()) {
                return; // buffer changed under us
            }
            if (!resolved || !resolved->hasCommand) {
                statusMessage_ = "Code lens has no runnable command.";
                return;
            }
            statusMessage_ = "Running " + (resolved->title.empty() ? resolved->commandName : resolved->title) + "...";
            lspManager_->ExecuteCommand(
                *bufferPtr, serverKey, resolved->commandName, resolved->commandArguments,
                [this](bool ok) { statusMessage_ = ok ? "Code lens command executed." : "Code lens command failed."; });
        });
}

void BufferView::RefreshCodeActionSelectStatus() {
    statusMessage_ = "Code action: ";
    if (!onCandidatesChanged_) {
        return;
    }
    ListPopupModel model;
    model.title = "Code action";
    model.rows.reserve(pendingCodeActions_.size());
    for (std::size_t i = 0; i < pendingCodeActions_.size(); ++i) {
        model.rows.push_back({.left = std::to_string(i + 1) + ")", .main = pendingCodeActions_[i].title});
    }
    if (!pendingCodeActions_.empty()) {
        model.selectedIndex = codeActionSelection_;
    }
    onCandidatesChanged_(std::move(model));
}

void BufferView::HandleCodeActionSelectKey(const editor::KeyChord& chord) {
    HandleChoicePromptKey({.count         = pendingCodeActions_.size(),
                           .selection     = &codeActionSelection_,
                           .cancelMessage = "Code action cancelled.",
                           .refresh       = [this] { RefreshCodeActionSelectStatus(); },
                           .commit        = [this, actions = pendingCodeActions_](
                                                std::size_t index) { ResolveAndApplyCodeAction(actions[index]); }},
                          chord);
}

void BufferView::ResolveAndApplyCodeAction(const editor::lsp::CodeAction& action) {
    // code-actions-resolve follow-up: a server (clangd included)
    // advertising resolveProvider deliberately sends this action back
    // without an edit yet -- codeAction/resolve fills it in, only now
    // that the user has actually chosen to apply it (see CodeAction::
    // resolvable's own doc comment in LspContent.h for why this isn't
    // done eagerly for every listed action). Fire-and-forget, same
    // async shape as every other LSP request here: the caller continues
    // immediately, ApplyCodeAction runs later from inside the callback
    // once the resolved edit actually arrives.
    if (action.resolvable && lspManager_) {
        text::Buffer* const bufferPtr = &activeBuffer_.Get();
        statusMessage_                = "Resolving \"" + action.title + "\"...";
        lspManager_->ResolveCodeAction(
            activeBuffer_.Get(), action,
            [this, bufferPtr, action](std::optional<editor::lsp::CodeAction> resolved) {
                if (bufferPtr != &activeBuffer_.Get()) {
                    return; // active buffer changed since the resolve request was sent
                }
                if (!resolved || (!resolved->hasEdit && !resolved->command)) {
                    statusMessage_ = "\"" + action.title + "\" could not be resolved.";
                    return;
                }
                // executeCommand follow-up: the spec only guarantees resolve
                // fills in "edit" -- a compliant server that doesn't echo
                // back a "command" the original action already carried must
                // not silently drop it here.
                if (!resolved->command && action.command) {
                    resolved->command = action.command;
                }
                ApplyCodeAction(*resolved);
            },
            codeActionServerKey_);
        return;
    }
    ApplyCodeAction(action);
}

void BufferView::ApplyProjectEdit(const std::vector<std::pair<text::Buffer*, std::vector<editor::lsp::WorkspaceTextEdit>>>& perBufferEdits,
                                  std::string                                                                               description) {
    editor::ProjectEditTransaction transaction;
    transaction.description = description;
    transaction.records.reserve(perBufferEdits.size());
    for (const auto& [buffer, edits] : perBufferEdits) {
        const std::size_t beforeSequence = buffer->CurrentUndoSequence();
        editor::lsp::ApplyWorkspaceTextEdits(*buffer, edits);
        if (buffer->Path()) {
            transaction.records.push_back(editor::ProjectUndoRecord{
                .path           = *buffer->Path(),
                .beforeSequence = beforeSequence,
                .afterSequence  = buffer->CurrentUndoSequence(),
            });
        }
        // A path-less (unsaved, never-saved-to-disk) buffer can't be
        // re-resolved by ProjectUndoManager::Undo/Redo later (they key on
        // BufferList::FindByPath) -- left out of the transaction, so its
        // own edit still undoes fine via plain per-buffer undo, it's just
        // not folded into the group.
    }
    if (projectUndo_) {
        projectUndo_->RecordTransaction(std::move(transaction));
    }
    statusMessage_ = description;
}

void BufferView::MaybeScheduleOnTypeFormatting(const editor::KeyChord& chord, std::size_t generationBefore) {
    if (inputMode_ != InputMode::Normal && inputMode_ != InputMode::Snippet) {
        return;
    }
    if (!editor::lsp::LspOnTypeFormattingEnabled()) {
        return;
    }
    if (chord.Control || chord.Meta) {
        return;
    }
    std::string ch;
    if (chord.Special == editor::SpecialKey::Enter) {
        // "newline"/"open-line" insert a real '\n' -- Enter never carries a
        // literal codepoint the way an ordinary self-insert keystroke does,
        // but it's exactly clangd's own firstTriggerCharacter (reindent
        // after a newline), so it needs its own mapping here rather than
        // being excluded the way MaybeScheduleSignatureHelp excludes every
        // SpecialKey.
        ch = "\n";
    }
    else if (chord.Special == editor::SpecialKey::None) {
        ch = text::EncodeCodepointUtf8(chord.Codepoint);
    }
    else {
        return; // no other special key inserts trigger-shaped text
    }
    if (activeBuffer_.Get().ContentGeneration() == generationBefore) {
        return; // nothing actually changed
    }
    if (!lspManager_) {
        return;
    }

    text::Buffer&                                              buffer      = activeBuffer_.Get();
    const std::size_t                                          point       = buffer.Point();
    const std::string                                          serverKey   = ResolvedLspServerKey(point);
    const std::string                                          languageKey = serverKey.empty() ? editor::LanguageKeyForMode(mode_) : serverKey;
    const std::optional<editor::lsp::OnTypeFormattingTriggers> triggers =
        lspManager_->OnTypeFormattingTriggersFor(lspManager_->ConnectionKeyForBuffer(buffer, languageKey));
    if (!triggers) {
        return; // this server never advertised documentOnTypeFormattingProvider at all
    }
    if (ch != triggers->first && std::find(triggers->more.begin(), triggers->more.end(), ch) == triggers->more.end()) {
        return; // not one of this server's own declared trigger characters
    }

    // Deliberately no debounce timer (unlike MaybeScheduleAutoCompletion/
    // MaybeScheduleSignatureHelp above): this only fires once per matching
    // trigger keystroke, not repeatedly while typing, so the request goes
    // out right away.
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   generation = onTypeFormattingRequest_.Begin();
    lspManager_->RequestOnTypeFormatting(
        buffer, point, ch,
        [this, bufferPtr, generation](std::optional<std::vector<editor::lsp::WorkspaceTextEdit>> edits) {
            if (onTypeFormattingRequest_.IsStale(generation) || bufferPtr != &activeBuffer_.Get()) {
                return; // superseded, or the active buffer changed under us
            }
            if (edits && !edits->empty()) {
                // A second, separate undo step from the keystroke that
                // triggered this -- the LSP round trip can't complete
                // synchronously within that keystroke's own dispatch, so
                // there's no existing mechanism to join the two (see this
                // method's own doc comment in BufferView.h).
                editor::lsp::ApplyWorkspaceTextEdits(*bufferPtr, *edits);
            }
        },
        serverKey);
}

void BufferView::RequestLspFormatThenSaveBuffer() {
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   generation = lspFormatOnSaveRequest_.Begin();
    statusMessage_                 = "Formatting...";
    lspManager_->RequestFormatting(
        buffer,
        [this, bufferPtr, generation](std::optional<std::vector<editor::lsp::WorkspaceTextEdit>> edits) {
            if (lspFormatOnSaveRequest_.IsStale(generation) || bufferPtr != &activeBuffer_.Get()) {
                return; // superseded, or the active buffer changed under us
            }
            text::Buffer& buffer       = *bufferPtr;
            const bool    formatFailed = !edits.has_value();
            if (edits && !edits->empty()) {
                editor::lsp::ApplyWorkspaceTextEdits(buffer, *edits); // one undo group -- see Step 0's fix
            }
            try {
                editor::WriteBufferToDisk(buffer);
                statusMessage_ = "Wrote " + buffer.Name() + (formatFailed ? " (LSP format failed)" : "");
            }
            catch (const std::exception& e) {
                statusMessage_ = e.what();
            }
            // Mirrors RunCommandAndHandleOutcome's own post-command refresh,
            // which the synchronous saveBufferBody path gets for free but
            // this async continuation must do itself, since it returns to
            // that method well before the save actually happens.
            RequestDiffForCurrentBuffer();
            viewport_.ScrollToShowPoint();
        },
        std::string{});
}

void BufferView::ApplyCodeAction(const editor::lsp::CodeAction& action) {
    if (action.touchesUnsupportedForm) {
        statusMessage_ = "\"" + action.title + "\" uses an unsupported edit form -- not applied.";
        return;
    }
    if (!action.hasEdit && !action.command) {
        statusMessage_ = "\"" + action.title + "\" has no edit or command to apply.";
        return;
    }

    // executeCommand follow-up: apply the edit first, then execute the
    // command -- spec order (some of harper-ls's own quickfixes carry
    // both). Neither step depends on the other's outcome; the edit is
    // synchronous, the command async, so the two failure paths report
    // independently rather than one gating the other.
    //
    // project-undo follow-up: a code action's edit can now touch more than
    // one file (previously refused via touchesOtherFiles above) -- resolved
    // and applied the same all-or-nothing, one-project-transaction way
    // ApplyRename already does. edit-application-gaps follow-up: a
    // "documentChanges" edit (file create/rename/delete alongside a symbol's
    // own edits -- a real refactor.extract/rewrite shape, not just a rename)
    // resolves and applies through the same ApplyResolvedWorkspaceEdit path.
    if (action.hasEdit && (!action.edits.empty() || !action.documentChangeOps.empty())) {
        editor::lsp::LspManager::ResolvedRename resolved;
        if (!action.edits.empty()) {
            const std::optional<std::vector<editor::lsp::LspManager::ResolvedRenameEdit>> resolvedEdits =
                editor::lsp::LspManager::ResolveCodeActionEdits(action);
            if (!resolvedEdits) {
                statusMessage_ = "\"" + action.title + "\" names a file this editor can't resolve -- not applied.";
                return;
            }
            resolved.edits = std::move(*resolvedEdits);
        }
        if (!action.documentChangeOps.empty()) {
            const std::optional<std::vector<editor::lsp::LspManager::ResolvedDocumentChangeOp>> resolvedOps =
                editor::lsp::LspManager::ResolveDocumentChangeOps(action.documentChangeOps);
            if (!resolvedOps) {
                statusMessage_ = "\"" + action.title + "\" names a file this editor can't resolve -- not applied.";
                return;
            }
            resolved.documentChangeOps = std::move(*resolvedOps);
        }
        resolved.hasEdit = !resolved.edits.empty() || !resolved.documentChangeOps.empty();
        if (!ApplyResolvedWorkspaceEdit(resolved, "Applied \"" + action.title + "\".")) {
            return; // ReportError/statusMessage_ already surfaced the failure
        }
    }
    if (!action.command) {
        statusMessage_ = "Applied \"" + action.title + "\".";
        return;
    }

    text::Buffer* const bufferPtr = &activeBuffer_.Get();
    const std::string   title     = action.title;
    statusMessage_                = "Applying \"" + title + "\"...";
    lspManager_->ExecuteCommand(activeBuffer_.Get(), codeActionServerKey_, action.command->name, action.command->arguments,
                                [this, bufferPtr, title](bool ok) {
                                    if (bufferPtr != &activeBuffer_.Get()) {
                                        return; // active buffer changed since the request was sent
                                    }
                                    statusMessage_ = ok ? "Applied \"" + title + "\"." : "\"" + title + "\" command failed.";
                                });
}

void BufferView::RequestDefinitionAtPoint(LspLocationKind kind) {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = definitionRequest_.Begin();
    // embedded-language-documents follow-up: routes to point's own embedded
    // server when it's inside one (e.g. an HTML <script> block), else "" ->
    // the host language, unchanged from before this feature existed.
    const std::string serverKey = ResolvedLspServerKey(point);
    // declaration/typeDefinition/implementation follow-up: the lowercase
    // word this request's status wording and pendingLocationLabel_ use.
    std::string label;
    switch (kind) {
        case LspLocationKind::Definition:
            label = "definition";
            break;
        case LspLocationKind::Declaration:
            label = "declaration";
            break;
        case LspLocationKind::TypeDefinition:
            label = "type definition";
            break;
        case LspLocationKind::Implementation:
            label = "implementation";
            break;
    }

    statusMessage_ = "Requesting " + label + "...";
    auto callback  = [this, bufferPtr, point, generation, label](std::vector<editor::lsp::LspManager::ResolvedLocation> locations) {
        if (definitionRequest_.IsStale(generation)) {
            return; // superseded by a newer request
        }
        if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
            return; // buffer/point changed since the request was sent -- see RequestCodeActionsAtPoint's own identical guard
        }
        pendingDefinitions_   = std::move(locations);
        pendingLocationLabel_ = label;
        if (pendingDefinitions_.empty()) {
            statusMessage_ = "No " + label + " found.";
            return;
        }
        if (pendingDefinitions_.size() == 1) {
            // No confirmation needed, unlike a code action -- opening a
            // file and moving point is trivially undoable/re-navigable,
            // nothing destructive to confirm.
            JumpToDefinition(pendingDefinitions_[0]);
            return;
        }
        definitionSelection_ = 0;
        inputMode_           = InputMode::LspGotoDefinitionSelect;
        RefreshDefinitionSelectStatus();
    };

    switch (kind) {
        case LspLocationKind::Definition:
            lspManager_->RequestDefinition(buffer, point, std::move(callback), serverKey);
            break;
        case LspLocationKind::Declaration:
            lspManager_->RequestDeclaration(buffer, point, std::move(callback), serverKey);
            break;
        case LspLocationKind::TypeDefinition:
            lspManager_->RequestTypeDefinition(buffer, point, std::move(callback), serverKey);
            break;
        case LspLocationKind::Implementation:
            lspManager_->RequestImplementation(buffer, point, std::move(callback), serverKey);
            break;
    }
}

void BufferView::RefreshDefinitionSelectStatus() {
    std::string label = pendingLocationLabel_;
    if (!label.empty()) {
        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    }
    std::string status = label + ": ";
    for (std::size_t i = 0; i < pendingDefinitions_.size(); ++i) {
        if (i > 0) {
            status += "  ";
        }
        const bool selected = (i == definitionSelection_);
        status += (selected ? "[" : "") + std::to_string(i + 1) + ") " + pendingDefinitions_[i].path.filename().string() +
                  ":" + std::to_string(pendingDefinitions_[i].position.line + 1) + (selected ? "]" : "");
    }
    statusMessage_ = status;
}

void BufferView::HandleDefinitionSelectKey(const editor::KeyChord& chord) {
    HandleChoicePromptKey({.count         = pendingDefinitions_.size(),
                           .selection     = &definitionSelection_,
                           .cancelMessage = "Go to definition cancelled.",
                           .refresh       = [this] { RefreshDefinitionSelectStatus(); },
                           .commit        = [this, locations = pendingDefinitions_](
                                                std::size_t index) { JumpToDefinition(locations[index]); }},
                          chord);
}

void BufferView::JumpToDefinition(const editor::lsp::LspManager::ResolvedLocation& location) {
    try {
        text::Buffer& opened = bufferList_.OpenOrCreateFile(location.path);
        PushJumpMark(); // before mutating point/activeBuffer_ -- see JumpMark's own doc comment
        activeBuffer_.Set(opened);
        opened.SetPoint(editor::lsp::LspPositionToByte(opened.Content(), location.position));
        statusMessage_.clear();
        viewport_.ScrollToShowPoint();
    }
    catch (const std::exception& e) {
        ReportError(e.what());
    }
}

void BufferView::RequestPeekDefinitionAtPoint() {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = peekDefinitionRequest_.Begin();
    const std::string   serverKey  = ResolvedLspServerKey(point);

    statusMessage_ = "Requesting definition...";
    auto callback  = [this, bufferPtr, point, generation](std::vector<editor::lsp::LspManager::ResolvedLocation> locations) {
        if (peekDefinitionRequest_.IsStale(generation)) {
            return; // superseded by a newer request
        }
        if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
            return; // buffer/point changed since the request was sent -- see RequestDefinitionAtPoint's own identical guard
        }
        if (locations.empty()) {
            statusMessage_ = "No definition found.";
            return;
        }
        pendingPeekDefinitions_  = std::move(locations);
        peekDefinitionSelection_ = 0;
        inputMode_               = InputMode::LspPeekDefinition;
        RefreshPeekDefinitionStatus();
    };
    lspManager_->RequestDefinition(buffer, point, std::move(callback), serverKey);
}

void BufferView::RefreshPeekDefinitionStatus() {
    const editor::lsp::LspManager::ResolvedLocation& location   = pendingPeekDefinitions_[peekDefinitionSelection_];
    const std::size_t                                targetLine = location.position.line + 1; // LSP lines are 0-indexed
    const std::size_t                                startLine  = targetLine > kPeekContextLinesBefore ? targetLine - kPeekContextLinesBefore : 1;
    const std::size_t                                endLine    = targetLine + kPeekContextLinesAfter;
    const std::string                                excerpt    = editor::multibuffer::ReadExcerptText(bufferList_, location.path, startLine, endLine);

    ListPopupModel model;
    model.title = location.path.filename().string() + ":" + std::to_string(targetLine);
    if (pendingPeekDefinitions_.size() > 1) {
        model.title += "  [" + std::to_string(peekDefinitionSelection_ + 1) + "/" +
                       std::to_string(pendingPeekDefinitions_.size()) + "]";
    }

    if (excerpt.empty()) {
        model.rows.push_back({.main = "(source unavailable)"});
    }
    else {
        std::size_t lineNumber = startLine;
        std::size_t pos        = 0;
        while (pos < excerpt.size()) {
            const std::size_t eol     = excerpt.find('\n', pos);
            const std::size_t lineEnd = (eol == std::string::npos) ? excerpt.size() : eol;
            std::string       text    = excerpt.substr(pos, lineEnd - pos);
            // Tabs would otherwise misalign the popup's own fixed-width box --
            // this codebase's real tab-aware column math (Buffer.h) isn't worth
            // pulling in for a plain read-only preview.
            std::replace(text.begin(), text.end(), '\t', ' ');
            if (lineNumber == targetLine) {
                model.selectedIndex = model.rows.size();
            }
            model.rows.push_back({.left = std::to_string(lineNumber), .main = std::move(text)});
            ++lineNumber;
            pos = (eol == std::string::npos) ? excerpt.size() : eol + 1;
        }
    }
    model.anchor = CompletionAnchorNow();

    statusMessage_ = "Peek definition: Enter opens, Esc closes" +
                     std::string(pendingPeekDefinitions_.size() > 1 ? ", Up/Down cycles" : "");
    if (onPeekChanged_) {
        onPeekChanged_(std::move(model));
    }
}

void BufferView::HandlePeekDefinitionKey(const editor::KeyChord& chord) {
    HandleChoicePromptKey({.count         = pendingPeekDefinitions_.size(),
                           .selection     = &peekDefinitionSelection_,
                           .cancelMessage = "Peek definition cancelled.",
                           .refresh       = [this] { RefreshPeekDefinitionStatus(); },
                           .commit        = [this, locations = pendingPeekDefinitions_](
                                                std::size_t index) { JumpToDefinition(locations[index]); }},
                          chord);
}

void BufferView::SetOnPeekChanged(std::function<void(std::optional<ListPopupModel>)> handler) {
    onPeekChanged_ = std::move(handler);
}

void BufferView::ActivatePeekDefinitionAt(std::size_t /*index*/) {
    if (inputMode_ != InputMode::LspPeekDefinition || pendingPeekDefinitions_.empty()) {
        return;
    }
    const editor::lsp::LspManager::ResolvedLocation location = pendingPeekDefinitions_[peekDefinitionSelection_];
    EndInteractiveSession();
    JumpToDefinition(location);
}

void BufferView::RequestHierarchyAtPoint(HierarchyDirection direction) {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = hierarchyRequest_.Begin();
    const std::string   serverKey  = ResolvedLspServerKey(point);

    std::string subjectLabel; // for "No <subjectLabel> at point." on an empty prepare result
    switch (direction) {
        case HierarchyDirection::IncomingCalls:
        case HierarchyDirection::OutgoingCalls:
            subjectLabel = "callable symbol";
            break;
        case HierarchyDirection::Supertypes:
        case HierarchyDirection::Subtypes:
            subjectLabel = "type";
            break;
    }

    statusMessage_  = "Requesting hierarchy...";
    auto onPrepared = [this, bufferPtr, point, generation, direction, serverKey,
                       subjectLabel](std::vector<editor::lsp::LspManager::ResolvedHierarchyItem> items) {
        if (hierarchyRequest_.IsStale(generation)) {
            return; // superseded by a newer request
        }
        if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
            return; // buffer/point changed since the request was sent -- see RequestDefinitionAtPoint's own identical guard
        }
        if (items.empty()) {
            statusMessage_ = "No " + subjectLabel + " at point.";
            return;
        }
        // overload-set follow-up (see this method's own doc comment in
        // BufferView.h): more than one match is rare enough that taking
        // the first is an acceptable v1 cut.
        editor::lsp::LspManager::ResolvedHierarchyItem root = std::move(items.front());
        HierarchySession                               session{.direction = direction, .buffer = bufferPtr, .serverKey = serverKey, .rootName = root.item.name};
        session.tree.Reset({std::move(root)});
        hierarchySession_       = std::move(session);
        hierarchySelectedIndex_ = 0;
        ExpandHierarchyNode(0); // auto-expand the root -- see this method's own doc comment in BufferView.h
    };

    switch (direction) {
        case HierarchyDirection::IncomingCalls:
        case HierarchyDirection::OutgoingCalls:
            lspManager_->RequestPrepareCallHierarchy(buffer, point, std::move(onPrepared), serverKey);
            break;
        case HierarchyDirection::Supertypes:
        case HierarchyDirection::Subtypes:
            lspManager_->RequestPrepareTypeHierarchy(buffer, point, std::move(onPrepared), serverKey);
            break;
    }
}

void BufferView::ExpandHierarchyNode(std::size_t index) {
    if (!hierarchySession_ || !lspManager_ || index >= hierarchySession_->tree.Size()) {
        return;
    }
    HierarchySession& session = *hierarchySession_;
    if (session.tree.IsLoading(index)) {
        return;
    }
    if (session.tree.ChildrenFetched(index)) {
        // Already explored -- just reveal it again, no request needed (see
        // this method's own doc comment in BufferView.h).
        session.tree.SetExpanded(index, true);
        PushHierarchyModel();
        return;
    }

    session.tree.BeginLoading(index);
    PushHierarchyModel(); // shows the loading glyph immediately

    const editor::lsp::HierarchyItem item       = session.tree.At(index).data.item;
    text::Buffer&                    buffer     = *session.buffer;
    const std::string                serverKey  = session.serverKey;
    const HierarchyDirection         direction  = session.direction;
    const std::size_t                generation = hierarchyRequest_.Begin();

    // find-references follow-up's own reasoning applies here too: a
    // superseded/stale response is simply dropped, not applied to
    // whatever the tree has become by the time it arrives.
    auto onItems = [this, index, generation](std::vector<editor::lsp::LspManager::ResolvedHierarchyItem> children) {
        if (!hierarchySession_ || hierarchyRequest_.IsStale(generation)) {
            return;
        }
        hierarchySession_->tree.Expand(index, std::move(children));
        PushHierarchyModel();
    };
    // callHierarchy/incomingCalls and .../outgoingCalls respond with the
    // extra fromRanges wrapper (LspManager::ResolvedHierarchyCall) --
    // call.callSites isn't surfaced in the tree yet (see LspManager.h's own
    // ResolvedHierarchyCall doc comment on that v1 cut), so this just
    // unwraps each entry's item and reuses onItems above.
    auto onCalls = [this, index, generation](std::vector<editor::lsp::LspManager::ResolvedHierarchyCall> calls) {
        if (!hierarchySession_ || hierarchyRequest_.IsStale(generation)) {
            return;
        }
        std::vector<editor::lsp::LspManager::ResolvedHierarchyItem> children;
        children.reserve(calls.size());
        for (editor::lsp::LspManager::ResolvedHierarchyCall& call : calls) {
            children.push_back(std::move(call.item));
        }
        hierarchySession_->tree.Expand(index, std::move(children));
        PushHierarchyModel();
    };

    switch (direction) {
        case HierarchyDirection::IncomingCalls:
            lspManager_->RequestIncomingCalls(buffer, item, std::move(onCalls), serverKey);
            break;
        case HierarchyDirection::OutgoingCalls:
            lspManager_->RequestOutgoingCalls(buffer, item, std::move(onCalls), serverKey);
            break;
        case HierarchyDirection::Supertypes:
            lspManager_->RequestSupertypes(buffer, item, std::move(onItems), serverKey);
            break;
        case HierarchyDirection::Subtypes:
            lspManager_->RequestSubtypes(buffer, item, std::move(onItems), serverKey);
            break;
    }
}

void BufferView::PushHierarchyModel() {
    if (!hierarchySession_) {
        if (onHierarchyChanged_) {
            onHierarchyChanged_(std::nullopt);
        }
        return;
    }

    const HierarchySession& session = *hierarchySession_;
    std::string             verb;
    switch (session.direction) {
        case HierarchyDirection::IncomingCalls:
            verb = "Callers of ";
            break;
        case HierarchyDirection::OutgoingCalls:
            verb = "Calls from ";
            break;
        case HierarchyDirection::Supertypes:
            verb = "Supertypes of ";
            break;
        case HierarchyDirection::Subtypes:
            verb = "Subtypes of ";
            break;
    }

    ui::TreeViewModel model;
    model.title = verb + session.rootName;

    const std::vector<editor::ExpandableTree<editor::lsp::LspManager::ResolvedHierarchyItem>::VisibleRow> rows =
        session.tree.FlattenVisible();
    model.rows.reserve(rows.size());
    for (const auto& row : rows) {
        const auto& node = session.tree.At(row.index);
        model.rows.push_back(ui::TreeRow{
            .label       = BuildHierarchyRowLabel(node.data),
            .depth       = row.depth,
            .hasChildren = !node.childrenFetched || !node.children.empty(),
            .expanded    = node.expanded,
            .loading     = node.loading,
        });
    }
    if (!model.rows.empty()) {
        model.selectedIndex = std::min(hierarchySelectedIndex_, model.rows.size() - 1);
    }

    if (onHierarchyChanged_) {
        onHierarchyChanged_(std::move(model));
    }
}

void BufferView::EndHierarchySession() {
    hierarchySession_.reset();
    hierarchySelectedIndex_ = 0;
    if (onHierarchyChanged_) {
        onHierarchyChanged_(std::nullopt);
    }
    TakeFocus(); // reclaim keyboard focus from the TreeView overlay
}

void BufferView::SetOnHierarchyChanged(std::function<void(std::optional<ui::TreeViewModel>)> handler) {
    onHierarchyChanged_ = std::move(handler);
}

void BufferView::HierarchyActivate(std::size_t index) {
    if (!hierarchySession_ || index >= hierarchySession_->tree.Size()) {
        return;
    }
    const editor::lsp::LspManager::ResolvedHierarchyItem& resolved = hierarchySession_->tree.At(index).data;
    const editor::lsp::LspManager::ResolvedLocation       location{.path = resolved.path, .position = resolved.item.position};
    EndHierarchySession();
    JumpToDefinition(location);
}

void BufferView::HierarchyToggleExpand(std::size_t index) {
    hierarchySelectedIndex_ = index;
    ExpandHierarchyNode(index);
}

void BufferView::HierarchyCollapse(std::size_t index) {
    if (!hierarchySession_ || index >= hierarchySession_->tree.Size()) {
        return;
    }
    hierarchySelectedIndex_ = index;
    hierarchySession_->tree.SetExpanded(index, false);
    PushHierarchyModel();
}

void BufferView::HierarchyCancel() {
    EndHierarchySession();
}

void BufferView::HierarchySelectionChanged(std::size_t index) {
    hierarchySelectedIndex_ = index;
}

void BufferView::PushJumpMark() {
    jumpBackStack_.push_back(JumpMark{activeBuffer_.Get().Name(), activeBuffer_.Get().Point()});
    if (jumpBackStack_.size() > kMaxJumpBackStack) {
        jumpBackStack_.erase(jumpBackStack_.begin());
    }
    // A fresh jump branches off the navigation history -- the old forward
    // path is no longer reachable, same as a browser discarding forward
    // history on a new navigation.
    jumpForwardStack_.clear();
}

void BufferView::JumpBack() {
    while (!jumpBackStack_.empty()) {
        const JumpMark mark = jumpBackStack_.back();
        jumpBackStack_.pop_back();
        if (text::Buffer* target = bufferList_.Find(mark.bufferName)) {
            // Leave a trail for jump-forward to retrace -- pushed directly,
            // not via PushJumpMark, since that would clear the very stack
            // being populated here.
            jumpForwardStack_.push_back(JumpMark{activeBuffer_.Get().Name(), activeBuffer_.Get().Point()});
            if (jumpForwardStack_.size() > kMaxJumpBackStack) {
                jumpForwardStack_.erase(jumpForwardStack_.begin());
            }
            activeBuffer_.Set(*target);
            target->SetPoint(mark.byteOffset); // Buffer::SetPoint already clamps out-of-range offsets
            statusMessage_.clear();
            viewport_.ScrollToShowPoint();
            return;
        }
        // buffer closed since the mark was pushed -- skip it, try the next one
    }
    statusMessage_ = "No more jump history.";
}

void BufferView::JumpForward() {
    while (!jumpForwardStack_.empty()) {
        const JumpMark mark = jumpForwardStack_.back();
        jumpForwardStack_.pop_back();
        if (text::Buffer* target = bufferList_.Find(mark.bufferName)) {
            jumpBackStack_.push_back(JumpMark{activeBuffer_.Get().Name(), activeBuffer_.Get().Point()});
            if (jumpBackStack_.size() > kMaxJumpBackStack) {
                jumpBackStack_.erase(jumpBackStack_.begin());
            }
            activeBuffer_.Set(*target);
            target->SetPoint(mark.byteOffset);
            statusMessage_.clear();
            viewport_.ScrollToShowPoint();
            return;
        }
        // buffer closed since the mark was pushed -- skip it, try the next one
    }
    statusMessage_ = "No further jump history.";
}

void BufferView::RequestDocumentSymbolsAtPoint() {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   generation = documentSymbolRequest_.Begin();
    const std::string   serverKey  = ResolvedLspServerKey(buffer.Point());

    statusMessage_ = "Requesting symbols...";
    lspManager_->RequestDocumentSymbols(
        buffer,
        [this, bufferPtr, generation](std::vector<editor::lsp::LspManager::SymbolResult> symbols) {
            if (documentSymbolRequest_.IsStale(generation)) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get()) {
                return; // buffer switched since the request was sent
            }
            if (symbols.empty()) {
                statusMessage_ = "No symbols found.";
                return;
            }
            documentSymbolCandidates_ = std::move(symbols);
            documentSymbolLabels_.clear();
            documentSymbolLabels_.reserve(documentSymbolCandidates_.size());
            for (const editor::lsp::LspManager::SymbolResult& symbol : documentSymbolCandidates_) {
                documentSymbolLabels_.push_back(BuildSymbolLabel(symbol, /*includePath=*/false));
            }
            documentSymbolSelection_ = 0;
            inputMode_               = InputMode::LspGotoSymbol;
            prompt_.emplace("Go to symbol (fuzzy): ");
            RefreshDocumentSymbolStatus();
        },
        serverKey);
}

void BufferView::RefreshDocumentSymbolStatus() {
    const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(documentSymbolLabels_, prompt_->Text());
    documentSymbolSelection_              = ranked.empty() ? 0 : std::min(documentSymbolSelection_, ranked.size() - 1);

    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(
            ranked.empty() ? std::nullopt
                           : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(), ranked, documentSymbolSelection_)));
    }
}

void BufferView::HandleDocumentSymbolKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(documentSymbolLabels_, prompt_->Text());
        if (ranked.empty()) {
            statusMessage_ = "No symbol matching \"" + prompt_->Text() + "\"";
            EndInteractiveSession();
            return;
        }
        const std::string& label = ranked[std::min(documentSymbolSelection_, ranked.size() - 1)];
        // Maps the ranked label back to its SymbolResult -- see
        // documentSymbolLabels_'s own doc comment for why exact-string
        // lookup is safe here.
        const auto it = std::find(documentSymbolLabels_.begin(), documentSymbolLabels_.end(), label);
        EndInteractiveSession();
        if (it == documentSymbolLabels_.end()) {
            return; // unreachable: every ranked label comes from documentSymbolLabels_ itself
        }
        const std::size_t                            index  = static_cast<std::size_t>(it - documentSymbolLabels_.begin());
        const editor::lsp::LspManager::SymbolResult& symbol = documentSymbolCandidates_[index];
        JumpToDefinition(editor::lsp::LspManager::ResolvedLocation{.path = symbol.path, .position = symbol.position});
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Go to symbol cancelled.";
        EndInteractiveSession();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        const std::vector<std::string> ranked = editor::FuzzyFilterAndRank(documentSymbolLabels_, prompt_->Text());
        if (!ranked.empty()) {
            documentSymbolSelection_ = chord.Special == editor::SpecialKey::Down
                                           ? (documentSymbolSelection_ + 1) % ranked.size()
                                           : (documentSymbolSelection_ + ranked.size() - 1) % ranked.size();
        }
        RefreshDocumentSymbolStatus();
        return;
    }
    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        documentSymbolSelection_ = 0;
        RefreshDocumentSymbolStatus();
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

void BufferView::RequestWorkspaceSymbolsForCurrentQuery() {
    // MaybeScheduleAutoCompletion/RequestCompletionAtPoint's own guard
    // shape: the debounce timer that leads here doesn't get cancelled when
    // the session ends, so this re-checks the session is still live first.
    if (inputMode_ != InputMode::LspWorkspaceSymbol || !lspManager_) {
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   generation = workspaceSymbolRequest_.Begin();
    const std::string   serverKey  = ResolvedLspServerKey(buffer.Point());
    const std::string   query      = prompt_->Text();

    lspManager_->RequestWorkspaceSymbols(
        buffer, query,
        [this, bufferPtr, generation](std::vector<editor::lsp::LspManager::SymbolResult> symbols) {
            if (workspaceSymbolRequest_.IsStale(generation)) {
                return; // superseded by a newer request
            }
            if (inputMode_ != InputMode::LspWorkspaceSymbol || bufferPtr != &activeBuffer_.Get()) {
                return; // session ended, or buffer switched, while this was in flight
            }
            pendingWorkspaceSymbols_ = std::move(symbols);
            workspaceSymbolLabels_.clear();
            workspaceSymbolLabels_.reserve(pendingWorkspaceSymbols_.size());
            for (const editor::lsp::LspManager::SymbolResult& symbol : pendingWorkspaceSymbols_) {
                workspaceSymbolLabels_.push_back(BuildSymbolLabel(symbol, /*includePath=*/true));
            }
            workspaceSymbolSelection_ =
                pendingWorkspaceSymbols_.empty() ? 0 : std::min(workspaceSymbolSelection_, pendingWorkspaceSymbols_.size() - 1);
            RefreshWorkspaceSymbolStatus();
        },
        serverKey);
}

void BufferView::RefreshWorkspaceSymbolStatus() {
    statusMessage_ = prompt_->StatusText();
    if (onCandidatesChanged_) {
        onCandidatesChanged_(workspaceSymbolLabels_.empty()
                                 ? std::nullopt
                                 : std::optional(BuildFuzzyCandidatePopupModel(prompt_->StatusText(), workspaceSymbolLabels_,
                                                                               workspaceSymbolSelection_)));
    }
}

void BufferView::HandleWorkspaceSymbolKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        if (pendingWorkspaceSymbols_.empty()) {
            statusMessage_ = "No symbol matching \"" + prompt_->Text() + "\"";
            EndInteractiveSession();
            return;
        }
        const editor::lsp::LspManager::SymbolResult symbol =
            pendingWorkspaceSymbols_[std::min(workspaceSymbolSelection_, pendingWorkspaceSymbols_.size() - 1)];
        EndInteractiveSession();
        JumpToDefinition(editor::lsp::LspManager::ResolvedLocation{.path = symbol.path, .position = symbol.position});
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Workspace symbol search cancelled.";
        EndInteractiveSession();
        return;
    }

    if (chord.Special == editor::SpecialKey::Down || chord.Special == editor::SpecialKey::Up) {
        if (!pendingWorkspaceSymbols_.empty()) {
            workspaceSymbolSelection_ = chord.Special == editor::SpecialKey::Down
                                            ? (workspaceSymbolSelection_ + 1) % pendingWorkspaceSymbols_.size()
                                            : (workspaceSymbolSelection_ + pendingWorkspaceSymbols_.size() - 1) %
                                                  pendingWorkspaceSymbols_.size();
        }
        RefreshWorkspaceSymbolStatus();
        return;
    }
    // Typing doesn't re-request immediately -- the server does its own
    // query-matching, so hammering it on every keystroke is real,
    // avoidable request volume (unlike the local-list pickers above, where
    // FuzzyFilterAndRank is nearly free). Echo the prompt text right away
    // regardless -- only the result list itself waits for the debounce.
    // Pure cursor movement (CursorMoved) needs neither.
    if (HandlePromptEditingKey(chord) == PromptEditOutcome::TextEdited) {
        workspaceSymbolSelection_ = 0;
        statusMessage_            = prompt_->StatusText();
        if (eventLoop_) {
            workspaceSymbolDebounceTimer_.Arm(*eventLoop_, std::chrono::milliseconds(editor::lsp::LspCompletionDebounceMs()),
                                              [this] { RequestWorkspaceSymbolsForCurrentQuery(); });
        }
    }
    // CursorMoved/NotHandled: nothing else consumes a key here -- stay in the prompt.
}

void BufferView::SwitchHeaderSource() {
    text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.Path()) {
        statusMessage_ = "Buffer has no associated file.";
        return;
    }
    const std::filesystem::path path = *buffer.Path();

    // header-source-switching follow-up: LSP absence (no manager, no client
    // running for this buffer's language) falls straight to the filesystem
    // heuristic -- unlike lsp-goto-definition, that's the normal path for
    // every language except C/C++, not an error.
    if (!lspManager_) {
        OpenHeaderSourceCounterpartOrReport(path);
        return;
    }

    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   generation = switchHeaderSourceRequest_.Begin();
    statusMessage_                 = "Switching header/source...";
    lspManager_->RequestSwitchSourceHeader(
        buffer, [this, bufferPtr, generation, path](std::optional<std::filesystem::path> counterpart) {
            if (switchHeaderSourceRequest_.IsStale(generation) || bufferPtr != &activeBuffer_.Get()) {
                return; // superseded/buffer switched since the request was sent
            }
            if (counterpart) {
                OpenHeaderSourceCounterpart(*counterpart);
                return;
            }
            OpenHeaderSourceCounterpartOrReport(path);
        });
}

void BufferView::OpenHeaderSourceCounterpartOrReport(const std::filesystem::path& path) {
    if (const auto counterpart = editor::headersource::FindCounterpart(path)) {
        OpenHeaderSourceCounterpart(*counterpart);
        return;
    }
    statusMessage_ = "No corresponding header/source file found.";
}

void BufferView::OpenHeaderSourceCounterpart(const std::filesystem::path& path) {
    try {
        text::Buffer& opened = bufferList_.OpenOrCreateFile(path);
        activeBuffer_.Set(opened);
        statusMessage_.clear();
        viewport_.ScrollToShowPoint();
    }
    catch (const std::exception& e) {
        ReportError(e.what());
    }
}

void BufferView::RequestPrepareRenameAtPoint() {
    const auto openPrompt = [this](const std::string& prefill) {
        inputMode_ = InputMode::LspRenameNewName;
        prompt_.emplace("New name: ");
        if (!prefill.empty()) {
            prompt_->SetText(prefill);
        }
        statusMessage_ = prompt_->StatusText();
    };
    if (!lspManager_) {
        openPrompt({}); // no LSP manager at all -- same unprefilled prompt lsp-rename always opened before this existed
        return;
    }
    text::Buffer&       buffer                     = activeBuffer_.Get();
    text::Buffer* const bufferPtr                  = &buffer;
    const std::size_t   point                      = buffer.Point();
    const std::size_t   generation                 = prepareRenameRequest_.Begin();
    const std::string   serverKey                  = ResolvedLspServerKey(point);
    const std::size_t   contentGenerationAtRequest = buffer.ContentGeneration();

    lspManager_->RequestPrepareRename(
        buffer, point,
        [this, bufferPtr, point, generation, contentGenerationAtRequest, openPrompt](
            std::optional<editor::lsp::PrepareRenameResult> result) {
            if (prepareRenameRequest_.IsStale(generation)) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point ||
                activeBuffer_.Get().ContentGeneration() != contentGenerationAtRequest) {
                return; // buffer/point/content changed since the request was sent
            }
            if (!result) {
                openPrompt({}); // transport error or an unimplemented method -- fall back, don't block renaming
                return;
            }
            if (!result->valid) {
                statusMessage_ = "Cannot rename the symbol at point.";
                return;
            }
            if (!result->hasRange) {
                openPrompt({}); // {defaultBehavior: true} -- renameable, but no range of its own to prefill from
                return;
            }
            const std::size_t rangeStart = editor::lsp::LspPositionToByte(bufferPtr->Content(), result->start);
            const std::size_t rangeEnd   = editor::lsp::LspPositionToByte(bufferPtr->Content(), result->end);
            const std::string prefill    = !result->placeholder.empty()
                                               ? result->placeholder
                                               : (rangeEnd > rangeStart ? bufferPtr->Content().Substring(rangeStart, rangeEnd - rangeStart)
                                                                        : std::string());
            openPrompt(prefill);
        },
        serverKey);
}

void BufferView::RequestRenameAtPoint(const std::string& newName) {
    if (!lspManager_) {
        statusMessage_ = "No LSP manager available.";
        return;
    }
    text::Buffer&       buffer     = activeBuffer_.Get();
    text::Buffer* const bufferPtr  = &buffer;
    const std::size_t   point      = buffer.Point();
    const std::size_t   generation = renameRequest_.Begin();
    // embedded-language-documents follow-up: see RequestDefinitionAtPoint's
    // identical comment above.
    const std::string serverKey = ResolvedLspServerKey(point);

    statusMessage_ = "Requesting rename...";
    lspManager_->RequestRename(
        buffer, point, newName,
        [this, bufferPtr, point, generation](std::optional<editor::lsp::LspManager::ResolvedRename> result) {
            if (renameRequest_.IsStale(generation)) {
                return; // superseded by a newer request
            }
            if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                return; // buffer/point changed since the request was sent
            }
            if (!result) {
                statusMessage_ = "Rename failed.";
                return;
            }
            if (result->touchesUnsupportedForm) {
                statusMessage_ = "Rename uses an unsupported edit form -- not applied.";
                return;
            }
            if (!result->hasEdit) {
                statusMessage_ = "No rename edits available.";
                return;
            }
            std::size_t fileCount = result->edits.size();
            std::size_t editCount = 0;
            for (const auto& edit : result->edits) {
                editCount += edit.edits.size();
            }
            // edit-application-gaps follow-up: a "documentChanges" rename
            // can also carry pure resource ops (a plain file rename/create/
            // delete with no text edits attached) alongside or instead of
            // EditFile entries -- counted separately so "0 edits across 0
            // files" doesn't read as a no-op when the rename is really just
            // moving a file.
            std::size_t opCount = 0;
            for (const auto& op : result->documentChangeOps) {
                if (op.kind == editor::lsp::DocumentChangeOp::Kind::EditFile) {
                    ++fileCount;
                    editCount += op.edits.size();
                }
                else {
                    ++opCount;
                }
            }
            renameTitle_ = std::to_string(editCount) + " edit" + (editCount == 1 ? "" : "s") + " across " +
                           std::to_string(fileCount) + " file" + (fileCount == 1 ? "" : "s");
            if (opCount > 0) {
                renameTitle_ += ", " + std::to_string(opCount) + " file op" + (opCount == 1 ? "" : "s");
            }

            ApplyRename(*result);
        },
        serverKey);
}

void BufferView::ApplyRename(const editor::lsp::LspManager::ResolvedRename& result) {
    ApplyResolvedWorkspaceEdit(result, "Renamed (" + renameTitle_ + ").");
}

bool BufferView::ApplyServerPushedWorkspaceEdit(const editor::lsp::LspManager::ResolvedRename& edit, const std::string& label) {
    return ApplyResolvedWorkspaceEdit(edit, "Applied \"" + label + "\" (server request).");
}

bool BufferView::ApplyResolvedWorkspaceEdit(const editor::lsp::LspManager::ResolvedRename& edit, std::string description) {
    if (edit.touchesUnsupportedForm || !edit.hasEdit) {
        statusMessage_ = description + " has no edit to apply.";
        return false;
    }

    // Resolve/apply everything through one try block -- a resource op
    // (Create/Rename/DeleteFile) that fails partway (an unresolvable path,
    // an already-existing create/rename target with neither overwrite nor
    // ignoreIfExists set, a missing delete target) leaves earlier steps'
    // filesystem side effects in place (they're not transactional the way
    // in-memory buffer edits are), but stops before touching anything else
    // -- the same "fail loud, don't guess" precedent HandleRenameFileKey/
    // HandleDeleteFileKey already establish for the equivalent
    // user-initiated commands.
    std::vector<std::pair<text::Buffer*, std::vector<editor::lsp::WorkspaceTextEdit>>> perBufferEdits;
    bool                                                                               touchedFilesystem = false;
    try {
        // Resolve (find-or-open) every "changes"-form buffer FIRST, same
        // all-or-nothing-open guarantee this method has always had.
        perBufferEdits.reserve(edit.edits.size() + edit.documentChangeOps.size());
        for (const editor::lsp::LspManager::ResolvedRenameEdit& renameEdit : edit.edits) {
            text::Buffer* buffer = bufferList_.FindByPath(renameEdit.path);
            if (!buffer) {
                buffer = &bufferList_.OpenFile(renameEdit.path);
            }
            perBufferEdits.emplace_back(buffer, renameEdit.edits);
        }

        // documentChangeOps apply strictly in order -- a resource op's
        // filesystem side effect must land before a later EditFile op that
        // targets the file it just created/renamed.
        using Kind = editor::lsp::DocumentChangeOp::Kind;
        for (const editor::lsp::LspManager::ResolvedDocumentChangeOp& op : edit.documentChangeOps) {
            switch (op.kind) {
                case Kind::CreateFile: {
                    const bool exists = std::filesystem::exists(op.path);
                    if (exists && op.ignoreIfExists) {
                        break;
                    }
                    if (exists && !op.overwrite) {
                        throw std::runtime_error("create: " + op.path.string() + " already exists");
                    }
                    text::Buffer& buffer = bufferList_.OpenOrCreateFile(op.path);
                    if (exists && op.overwrite) {
                        buffer.BeginUndoGroup();
                        buffer.DeleteRange(0, buffer.Content().ByteLength());
                        buffer.EndUndoGroup();
                    }
                    touchedFilesystem = true;
                    break;
                }
                case Kind::DeleteFile: {
                    if (!std::filesystem::exists(op.path)) {
                        if (op.ignoreIfNotExists) {
                            break;
                        }
                        throw std::runtime_error("delete: " + op.path.string() + " does not exist");
                    }
                    editor::DeleteProjectPath(op.path);
                    touchedFilesystem = true;
                    break;
                }
                case Kind::RenameFile: {
                    const bool targetExists = std::filesystem::exists(op.path);
                    if (targetExists && !op.overwrite && !op.ignoreIfExists) {
                        throw std::runtime_error("rename: " + op.path.string() + " already exists");
                    }
                    if (targetExists && op.ignoreIfExists && !op.overwrite) {
                        break; // target already there, told to leave it alone
                    }
                    if (targetExists && op.overwrite) {
                        editor::DeleteProjectPath(op.path); // RenameProjectPath refuses onto an existing target
                    }
                    editor::RenameProjectPath(op.oldPath, op.path);
                    if (text::Buffer* buffer = bufferList_.FindByPath(op.oldPath)) {
                        buffer->SetPath(op.path);
                        buffer->Rename(op.path.filename().string());
                        editor::ClearModeCacheFor(*buffer);
                    }
                    touchedFilesystem = true;
                    break;
                }
                case Kind::EditFile: {
                    text::Buffer* buffer = bufferList_.FindByPath(op.path);
                    if (!buffer) {
                        buffer = &bufferList_.OpenFile(op.path);
                    }
                    perBufferEdits.emplace_back(buffer, op.edits);
                    break;
                }
            }
        }
    }
    catch (const std::exception& e) {
        ReportError(description + " failed: " + e.what(), editor::LogCategory::Lsp);
        return false;
    }

    ApplyProjectEdit(perBufferEdits, description);
    if (touchedFilesystem && projectSidebar_) {
        projectSidebar_->InvalidateTree();
    }
    return true;
}

std::vector<std::string> BufferView::GatherPathCompletionCandidates() const {
    if (inputMode_ == InputMode::FindFile || inputMode_ == InputMode::OpenProjectPath) {
        return text::CompleteFilePath(prompt_->Text());
    }
    if (inputMode_ == InputMode::FindScratch) {
        return editor::CompleteScratchNames(prompt_->Text());
    }
    return {};
}

// dropdown-path-completion follow-up: FindFile/OpenProjectPath/FindScratch's
// own live dropdown -- unlike the selection-based Handle<Mode>Key sessions
// (switch-project/switch-to-buffer/vcs-switch-branch/acp-agent-name), Enter
// still finalizes on literal prompt_->Text() in HandlePromptKey, unchanged,
// since typing a path/name with no match is a valid "create new" action
// here (a new file, a new scratch pad, a new project root). This popup is a
// visual + Tab-to-accept aid only, called on session start and on every
// keystroke/Up/Down (see HandlePromptKey's own call sites).

void BufferView::RefreshPathCompletionPopup() {
    const std::vector<std::string> candidates = GatherPathCompletionCandidates();
    pathCompletionSelection_                  = candidates.empty() ? 0 : std::min(pathCompletionSelection_, candidates.size() - 1);

    statusMessage_ = prompt_->StatusText();
    if (!onCandidatesChanged_) {
        return;
    }
    if (candidates.empty()) {
        onCandidatesChanged_(std::nullopt);
        return;
    }
    const bool                                           isPathMode = inputMode_ == InputMode::FindFile || inputMode_ == InputMode::OpenProjectPath;
    const std::function<std::string(const std::string&)> display =
        isPathMode ? std::function<std::string(const std::string&)>(MaskPathCandidateToLastSegment)
                   : std::function<std::string(const std::string&)>{};
    onCandidatesChanged_(
        BuildFuzzyCandidatePopupModel(prompt_->StatusText(), candidates, pathCompletionSelection_, display));
}

void BufferView::RequestProjectFindReferences() {
    text::Buffer&             buffer  = activeBuffer_.Get();
    const text::ITextStorage& content = buffer.Content();

    const std::optional<std::pair<std::size_t, std::size_t>> wordRegion = WordRegionAtPoint(content, buffer.Point());
    if (!wordRegion) {
        statusMessage_ = "No identifier at point.";
        return;
    }
    const std::string word = content.Substring(wordRegion->first, wordRegion->second - wordRegion->first);

    // find-references follow-up: prefer a real semantic answer from the
    // language server when one is actually running for this buffer's
    // language -- the same "is one currently usable" StatusForLanguage
    // check RequestCompletionAtPoint's own dabbrev-fallback already uses,
    // not a guess. Falls through to the plain-text scan below when no
    // server is running (nothing configured, still spawning, crashed, ...),
    // the same "LSP is a nice-to-have accelerant, not the only path"
    // precedent SwitchHeaderSource already established -- unlike
    // lsp-goto-definition/-declaration/etc., which refuse outright with no
    // LSP manager, this command already has a working universal fallback
    // worth keeping.
    const std::size_t point         = buffer.Point();
    const std::string serverKey     = ResolvedLspServerKey(point);
    const std::string languageKey   = serverKey.empty() ? editor::LanguageKeyForMode(mode_) : serverKey;
    const bool        hasRunningLsp = lspManager_ && lspManager_->StatusForLanguage(lspManager_->ConnectionKeyForBuffer(
                                                         buffer, languageKey)) == editor::lsp::LspManager::LspStatus::Running;

    if (hasRunningLsp) {
        text::Buffer* const bufferPtr  = &buffer;
        const std::size_t   generation = referencesRequest_.Begin();
        statusMessage_                 = "Requesting references...";
        lspManager_->RequestReferences(
            buffer, point,
            [this, bufferPtr, point, generation, word](std::vector<editor::lsp::LspManager::ResolvedLocation> locations) {
                if (referencesRequest_.IsStale(generation)) {
                    return; // superseded by a newer request
                }
                if (bufferPtr != &activeBuffer_.Get() || activeBuffer_.Get().Point() != point) {
                    return; // buffer/point changed since the request was sent
                }
                if (locations.empty()) {
                    statusMessage_ = "No references to \"" + word + "\" found.";
                    return;
                }

                const std::filesystem::path                     root = editor::ProjectRoot();
                std::vector<editor::multibuffer::ExcerptSource> excerpts;
                excerpts.reserve(locations.size());
                for (const editor::lsp::LspManager::ResolvedLocation& location : locations) {
                    const std::size_t           lineNumber = location.position.line + 1; // LSP is 0-indexed, excerpts/SearchMatch are 1-indexed
                    std::error_code             ec;
                    const std::filesystem::path relative    = std::filesystem::relative(location.path, root, ec);
                    const std::string           displayPath = (!ec && !relative.empty()) ? relative.string() : location.path.string();
                    excerpts.push_back(editor::multibuffer::ExcerptSource{
                        location.path, lineNumber, lineNumber,
                        "▸ " + displayPath + ":" + std::to_string(lineNumber), // same disclosure-triangle convention as the text-search path below
                        ReadFileLine(location.path, lineNumber),
                        {},
                        /*editable=*/true});
                }

                text::Buffer& results = editor::multibuffer::BuildMultibuffer(bufferList_, "*references: " + word + "*", excerpts);
                editor::SetLastResultsBuffer("*references: " + word + "*");
                activeBuffer_.Set(results);
                statusMessage_ = std::to_string(locations.size()) + " reference" + (locations.size() == 1 ? "" : "s") + " to \"" +
                                 word + "\" -- C-c v v to visit";
            },
            serverKey);
        return;
    }

    std::vector<editor::SearchMatch> matches;
    try {
        // "\bword\b" -- safe to embed the word unescaped: WordRegionAtPoint
        // only ever admits [A-Za-z0-9_], none of them RE2 metacharacters.
        matches = editor::SearchDirectory(editor::ProjectRoot(), "\\b" + word + "\\b");
    }
    catch (const editor::SearchPatternError& e) {
        ReportError(std::string("Invalid regex: ") + e.what());
        return;
    }

    if (matches.empty()) {
        statusMessage_ = "No references to \"" + word + "\" found.";
        return;
    }

    const std::filesystem::path                     root = editor::ProjectRoot();
    std::vector<editor::multibuffer::ExcerptSource> excerpts;
    excerpts.reserve(matches.size());
    for (const editor::SearchMatch& match : matches) {
        std::error_code             ec;
        const std::filesystem::path relative    = std::filesystem::relative(match.file, root, ec);
        const std::string           displayPath = (!ec && !relative.empty()) ? relative.string() : match.file.string();

        excerpts.push_back(editor::multibuffer::ExcerptSource{
            match.file, match.lineNumber, match.lineNumber,
            "▸ " + displayPath + ":" + std::to_string(match.lineNumber), // U+25B8, same disclosure triangle vcs-full-diff-buffer's headers use
            match.lineText,
            {},
            /*editable=*/true});
    }

    text::Buffer& results = editor::multibuffer::BuildMultibuffer(bufferList_, "*references: " + word + "*", excerpts);
    editor::SetLastResultsBuffer("*references: " + word + "*");
    activeBuffer_.Set(results);
    statusMessage_ = std::to_string(matches.size()) + " reference" + (matches.size() == 1 ? "" : "s") + " to \"" + word +
                     "\" -- C-c v v to visit";
}

void BufferView::RequestProjectFindReferencesForTesting() {
    RequestProjectFindReferences();
}

void BufferView::RequestHierarchyAtPointForTesting(HierarchyDirection direction) {
    RequestHierarchyAtPoint(direction);
}

void BufferView::OpenLinkAtPointWithoutLsp() {
    text::Buffer& buffer = activeBuffer_.Get();

    if (mode_.name == "org-mode") {
        if (const auto orgLink = editor::org::LinkAtPoint(buffer)) {
            if (!orgLink->target.empty() && (orgLink->target.front() == '*' || orgLink->target.front() == '#')) {
                // Real Org's own internal-link forms: "[[*Some Headline]]"
                // (matched against a headline's title) and "[[#custom-id]]"
                // (matched against a headline's own :CUSTOM_ID: property,
                // property-drawers follow-up).
                const bool        isCustomId = orgLink->target.front() == '#';
                const std::string ref        = orgLink->target.substr(1);
                const auto        lineStartByte =
                    isCustomId ? editor::org::FindHeadlineByCustomId(buffer.Text(), ref)
                               : editor::org::FindHeadlineByTitle(buffer.Text(), ref);
                if (lineStartByte) {
                    buffer.ClearMark();
                    buffer.SetPoint(*lineStartByte);
                    statusMessage_.clear();
                    viewport_.ScrollToShowPoint();
                }
                else {
                    statusMessage_ = "Link target not found: " + ref;
                }
                return;
            }
            // Not an internal link -- an Org link to a URL or a file path is
            // still just a URL or a file path once its brackets are
            // stripped away, so this reuses the exact same open/report tail
            // the generic (non-Org) path below uses.
            OpenDetectedLink(editor::link::DetectedLink{
                .kind      = editor::link::ClassifyTarget(orgLink->target),
                .target    = orgLink->target,
                .startByte = orgLink->startByte,
                .endByte   = orgLink->endByte,
            });
            return;
        }
        // Point isn't on a bracket link -- fall through to the same generic
        // bare-URL/file detection every other mode uses, the same
        // "org-specific first, generic fallback" chain org-cycle's own body
        // already established for fold-cycle -> table-align.
    }

    // import-target-tree-sitter follow-up: a mode with an import query
    // configured (Mode::importTarget) gets first refusal, the same
    // "mode-specific first, generic fallback" chain the org-mode block above
    // already established -- generalized across every language uniformly
    // (including a future dynamically-loaded one) since this branches on
    // whether the function is set, never on mode_.name.
    if (mode_.importTarget) {
        if (const auto imported = mode_.importTarget(buffer.Text(), buffer.Point())) {
            // resolver-gaps follow-up: PHP's own `use` namespace resolves
            // via a dedicated PSR-4 lookup against composer.json, not
            // ResolveFileLink's generic baseDirectory/ProjectRoot/
            // includePaths search at all -- an empty/whitespace-only
            // namespace (a malformed capture) falls through to the generic
            // link detection below rather than looking anything up.
            if (imported->isNamespacePath) {
                if (!imported->target.empty()) {
                    if (const auto resolved = editor::php::ResolvePsr4Namespace(imported->target, editor::ProjectRoot())) {
                        try {
                            text::Buffer& opened = bufferList_.OpenOrCreateFile(*resolved);
                            activeBuffer_.Set(opened);
                            statusMessage_.clear();
                        }
                        catch (const std::exception& e) {
                            ReportError(e.what());
                        }
                    }
                    else {
                        statusMessage_ = "No PSR-4 mapping found for: " + imported->target;
                    }
                    return;
                }
            }
            else {
                std::string target = imported->target;
                if (imported->isModulePath) {
                    std::replace(target.begin(), target.end(), '.', '/');
                }
                OpenDetectedLink(editor::link::DetectedLink{
                    .kind             = editor::link::LinkKind::File,
                    .target           = std::move(target),
                    .startByte        = imported->startByte,
                    .endByte          = imported->endByte,
                    .relativeLevel    = imported->relativeLevel,
                    .isModDeclaration = imported->isModDeclaration,
                });
                return;
            }
        }
        // No import at point under this mode's own query -- fall through to
        // the generic path below (e.g. a URL or bare file path elsewhere on
        // the same line).
    }

    const auto detected = editor::link::DetectLinkAtPoint(buffer.Text(), buffer.Point());
    if (!detected) {
        statusMessage_ = "No link at point.";
        return;
    }
    OpenDetectedLink(*detected);
}

void BufferView::StartRenameFileAt(const std::filesystem::path& path) {
    if (inputMode_ != InputMode::Normal) {
        statusMessage_ = "Finish the current prompt first.";
        return;
    }
    if (!std::filesystem::exists(path)) {
        statusMessage_ = "No such file or directory: " + path.string();
        return;
    }
    inputMode_    = InputMode::RenameFile;
    renameStage_  = RenameFileStage::EnteringDestination;
    renameSource_ = path;
    prompt_.emplace("Rename \"" + path.string() + "\" to: ");
    prompt_->SetText(path.string());
    statusMessage_ = prompt_->StatusText();
}

void BufferView::HandleRenameFileKey(const editor::KeyChord& chord) {
    if (chord.Special == editor::SpecialKey::Enter) {
        const std::string input = prompt_->Text();

        if (renameStage_ == RenameFileStage::EnteringSource) {
            if (!std::filesystem::exists(input)) {
                statusMessage_ = "No such file or directory: " + input;
                EndInteractiveSession();
                return;
            }
            renameSource_ = input;
            renameStage_  = RenameFileStage::EnteringDestination;
            prompt_.emplace("Rename \"" + renameSource_.string() + "\" to: ");
            statusMessage_ = prompt_->StatusText();
            return;
        }

        // EnteringDestination
        const std::filesystem::path destination = input;
        const std::filesystem::path source      = renameSource_;
        EndInteractiveSession();
        PerformProjectRename(source, destination);
        return;
    }
    if (IsQuit(chord)) {
        statusMessage_ = "Rename cancelled.";
        EndInteractiveSession();
        return;
    }
    (void)HandlePromptEditingKey(chord);
    statusMessage_ = prompt_->StatusText();
}

void BufferView::PerformProjectRename(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::error_code ec;
    if (!std::filesystem::exists(source, ec)) {
        ReportError("ned: does not exist: " + source.string());
        return;
    }

    // Snapshot every open buffer's canonical path *before* the actual
    // rename happens on disk -- weakly_canonical needs real ancestors to
    // resolve symlinks through, and once source (or an ancestor of a
    // buffer nested inside it, for a directory rename) is gone, there's
    // nothing left on disk at the old location to resolve against.
    std::vector<std::pair<text::Buffer*, std::filesystem::path>> openBuffers;
    for (const auto& candidate : bufferList_.Buffers()) {
        if (candidate->Path().has_value()) {
            openBuffers.emplace_back(candidate.get(), std::filesystem::weakly_canonical(*candidate->Path()));
        }
    }
    const std::filesystem::path sourceCanonical = std::filesystem::weakly_canonical(source);

    // rename-file-notifications follow-up: one (oldPath, newPath) pair per
    // real file this rename touches -- a directory rename touches every
    // file nested inside it, each needing its own pair for
    // LspManager::RequestWillRenameFiles/NotifyFilesRenamed's per-file glob
    // matching (a server's filter is typically an extension glob like
    // "**/*.ts", which only makes sense matched per file, not against the
    // renamed directory's own path). Walked from source, which must still
    // exist on disk at this point -- see the existence check above.
    std::vector<editor::lsp::LspManager::FileRenameEntry> renamedFiles;
    if (std::filesystem::is_directory(source, ec)) {
        std::error_code walkEc;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 source, std::filesystem::directory_options::skip_permission_denied, walkEc)) {
            if (entry.is_regular_file(walkEc)) {
                const std::filesystem::path relative = entry.path().lexically_relative(source);
                renamedFiles.push_back({entry.path(), destination / relative});
            }
        }
    }
    else {
        renamedFiles.push_back({source, destination});
    }

    // rename-file-notifications follow-up: the actual rename+buffer-
    // bookkeeping, deferred behind LspManager::RequestWillRenameFiles below
    // -- captured by value since source/destination/openBuffers/
    // sourceCanonical/renamedFiles must all outlive this call, and a fresh
    // rename session started before this one's round trip completes must
    // not be able to invalidate any of them (they're locals here, not the
    // renameSource_/renameStage_ members EndInteractiveSession already
    // reset before this method was ever called).
    auto finishRename = [this, source, destination, sourceCanonical, openBuffers, renamedFiles](
                            std::optional<editor::lsp::LspManager::ResolvedRename> willRenameEdit) {
        // Applied BEFORE the actual rename below, per spec's intended use:
        // a server's willRenameFiles response typically fixes up *other*
        // files' import paths while source still exists at its pre-rename
        // location.
        if (willRenameEdit) {
            ApplyResolvedWorkspaceEdit(*willRenameEdit, "Fixed up references before rename");
        }
        try {
            editor::RenameProjectPath(source, destination);

            // Any open buffer whose file *was* source itself, or was
            // nested inside it (renaming a directory that has open buffers
            // somewhere underneath it), follows to its new location so it
            // doesn't end up silently pointing at a now-nonexistent path.
            // Only the destination's on-disk existence changes here -- a
            // buffer's own Name() only changes for an exact match, since a
            // nested buffer's filename is unaffected by an ancestor
            // directory being renamed.
            for (auto& [buffer, canonicalPath] : openBuffers) {
                if (canonicalPath == sourceCanonical) {
                    buffer->SetPath(destination);
                    buffer->Rename(destination.filename().string());
                    // per-buffer-mode-cache follow-up: a rename can change
                    // which Mode applies (e.g. .txt -> .cpp) without
                    // touching content at all -- CachedModeForBuffer would
                    // otherwise keep returning the Mode built for the old
                    // path forever, since its cache is keyed purely by
                    // buffer identity with no path check of its own.
                    editor::ClearModeCacheFor(*buffer);
                }
                else if (const std::filesystem::path relative = canonicalPath.lexically_relative(sourceCanonical);
                         !relative.empty() && *relative.begin() != "..") {
                    buffer->SetPath(destination / relative);
                    editor::ClearModeCacheFor(*buffer);
                }
            }

            statusMessage_ = "Renamed to " + destination.string();
            if (projectSidebar_) {
                projectSidebar_->InvalidateTree();
            }
            if (lspManager_) {
                lspManager_->NotifyFilesRenamed(renamedFiles);
            }
        }
        catch (const std::exception& e) {
            ReportError(e.what());
        }
    };

    if (lspManager_) {
        lspManager_->RequestWillRenameFiles(renamedFiles, std::move(finishRename));
    }
    else {
        finishRename(std::nullopt);
    }
}

void BufferView::SetLspManager(editor::lsp::LspManager* lspManager) {
    lspManager_ = lspManager;
}

} // namespace ned::ui
