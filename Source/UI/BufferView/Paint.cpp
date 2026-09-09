//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// Everything that draws: Paint() and its row/overlay/sticky-scroll helpers, plus the
// per-cell brush and selection predicates it consults.
//

#include "UI/BufferView/Internal.h"

namespace ned::ui {

// The file-local helpers these definitions call live in BufferView/Internal.h
// now that several parts share them -- see that header. This using-directive is
// what let the split leave every call site untouched.
using namespace detail;

std::size_t BufferView::AnnotationRowsForLine(std::size_t line) const {
    if (!editor::InlineDiagnosticsEnabled()) {
        return 0;
    }
    return gutters_.InlineDiagnosticsByLine().contains(line) ? 1 : 0;
}

std::size_t BufferView::LeadingAnnotationRowsForLine(std::size_t line) const {
    if (!lspManager_ || !editor::lsp::LspCodeLensEnabled()) {
        return 0;
    }
    text::Buffer&             buffer  = activeBuffer_.Get();
    const text::ITextStorage& content = buffer.Content();
    if (line >= content.LineCount()) {
        return 0;
    }
    const std::size_t lineStart = content.LineToByteOffset(line);
    const std::size_t lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    for (const auto& lens : lspManager_->CodeLensSpans(buffer)) {
        if (lens.startByte >= lineStart && lens.startByte < lineEnd) {
            return 1;
        }
        // A lens anchored at an empty line's own zero-width range (lineEnd
        // == lineStart there) would never satisfy `< lineEnd` above --
        // caught here instead, the same edge case InlayHintsForLine's own
        // [lineStart, lineEnd) convention doesn't need to worry about
        // (an inlay hint is never the only content on its own line).
        if (lineStart == lineEnd && lens.startByte == lineStart) {
            return 1;
        }
    }
    return 0;
}

void BufferView::PaintCodeLensRow(Canvas& c, int row, std::size_t line, std::size_t gutterWidth) const {
    text::Buffer&             buffer  = activeBuffer_.Get();
    const text::ITextStorage& content = buffer.Content();
    if (line >= content.LineCount() || !lspManager_) {
        return;
    }
    const std::size_t lineStart = content.LineToByteOffset(line);
    const std::size_t lineEnd =
        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();

    std::string joinedTitle;
    for (const auto& lens : lspManager_->CodeLensSpans(buffer)) {
        const bool onThisLine =
            (lens.startByte >= lineStart && lens.startByte < lineEnd) || (lineStart == lineEnd && lens.startByte == lineStart);
        if (!onThisLine || lens.title.empty()) {
            continue;
        }
        if (!joinedTitle.empty()) {
            joinedTitle += " | ";
        }
        joinedTitle += lens.title;
    }
    if (joinedTitle.empty()) {
        return; // shouldn't happen (RowsForLine/LeadingAnnotationRowsForLine agree with this scan) -- leave the blanked row
    }

    const Brush titleBrush{.background = theme_.background, .foreground = theme_.ghostTextForeground, .italic = true};
    const int   width = c.size().width;
    const int   col   = static_cast<int>(gutterWidth);
    // A lens title comes from real LSP text (symbol names, reference counts,
    // author names for a blame-style lens, ...) and isn't guaranteed ASCII --
    // PaintUtf8Row (Border.h) steps by codepoint rather than by raw byte, so
    // a multi-byte codepoint lands in exactly one cell instead of having its
    // continuation bytes each claim their own (invalid, unrenderable) cell.
    if (col < width) {
        PaintUtf8Row(c, col, row, joinedTitle, titleBrush, width - col);
    }
}

std::vector<editor::SymbolMarker> BufferView::StickyScrollChainForCurrentViewport() const {
    if (!editor::StickyScrollEnabled()) {
        return {};
    }
    const int maxRows = editor::StickyScrollMaxRows();
    if (maxRows <= 0) {
        return {};
    }
    if (gutters_.SymbolMarkers().empty()) {
        return {};
    }

    const text::Buffer&               buffer          = activeBuffer_.Get();
    const text::ITextStorage&         content         = buffer.Content();
    const std::size_t                 viewportTopByte = content.LineToByteOffset(viewport_.TopLine());
    std::vector<editor::SymbolMarker> chain =
        editor::stickyscroll::StickyChainForViewportTop(gutters_.SymbolMarkers(), viewportTopByte);
    if (static_cast<int>(chain.size()) > maxRows) {
        // Keep the INNERMOST rows (nearest ancestors) when the chain runs
        // deeper than the cap -- the immediate enclosing context is more
        // useful than the outermost namespace once space is tight.
        chain.erase(chain.begin(), chain.begin() + (static_cast<int>(chain.size()) - maxRows));
    }
    return chain;
}

int BufferView::PaintStickyScrollRows(Canvas& c, std::size_t gutterWidth) const {
    const std::vector<editor::SymbolMarker> chain = StickyScrollChainForCurrentViewport();
    if (chain.empty()) {
        return 0;
    }

    const text::ITextStorage& content = activeBuffer_.Get().Content();
    const int                 width   = c.size().width;

    // sticky-scroll-gutter-alignment follow-up: mirrors GutterWidth()/
    // Paint()'s own [dap][diff][status][diagnostic][gap][digits][gap][test]
    // [coverage][symbol][fold][blame] column layout (see those methods' own doc
    // comments), recomputed here from the same Active()-flag primitives
    // rather than trusting gutterWidth as a second source of truth -- the
    // same "recompute, don't unpack" precedent OnMouseEvent's own foldStart
    // derivation already follows. Only digitsStart/gutterDigits and
    // symbolStart are needed: those are the two columns a sticky row
    // actually paints into, so its line number and glyph land in the exact
    // same screen columns an ordinary content row's own digits/symbol glyph
    // do -- a sticky row reads as a frozen real gutter row, not a
    // synthesized label. Fold/blame never get anything drawn into them here.
    const std::size_t diffColumnWidth     = DiffGutterActive() ? kDiffWidth : 0;
    const std::size_t dapColumnWidth      = DapGutterActive() ? kDapWidth : 0;
    const std::size_t diagnosticStart     = dapColumnWidth + diffColumnWidth + kStatusWidth;
    const std::size_t lineNumberGapWidth  = LineNumberGutterActive() ? kLineNumberGap : 0;
    const std::size_t digitsStart         = diagnosticStart + kDiagnosticWidth + lineNumberGapWidth;
    const std::size_t gutterDigits        = LineNumberGutterActive() ? std::to_string(content.LineCount()).size() : 0;
    const std::size_t testColumnWidth     = gutters_.TestGutterActive() ? kTestWidth : 0;
    const std::size_t coverageColumnWidth = gutters_.CoverageGutterActive() ? kCoverageWidth : 0;
    const std::size_t symbolStart         = digitsStart + gutterDigits + lineNumberGapWidth + testColumnWidth + coverageColumnWidth;

    for (std::size_t i = 0; i < chain.size(); ++i) {
        const editor::SymbolMarker& marker = chain[i];
        const int                   row    = static_cast<int>(i);

        for (int col = 0; col < width; ++col) {
            Cell& cell     = c[{.x = col, .y = row}];
            cell.character = " ";
            theme_.tabBar.ApplyTo(cell);
        }

        const std::size_t line = content.ByteOffsetToLine(marker.startByte);

        // Line number -- the real gutter's own digits column, right-aligned
        // to gutterDigits exactly like an ordinary content row. Always
        // absolute (not vim relativenumber-aware): this names "where this
        // header lives," not a motion distance from point.
        if (LineNumberGutterActive()) {
            const std::string lineNumber = std::to_string(line + 1);
            const std::size_t padding    = gutterDigits > lineNumber.size() ? gutterDigits - lineNumber.size() : 0;
            const Brush       lineNumberBrush{.background = theme_.tabBar.background, .foreground = theme_.lineNumberForeground};
            for (std::size_t k = 0; k < lineNumber.size() && static_cast<int>(digitsStart + padding + k) < width; ++k) {
                Cell& cell     = c[{.x = static_cast<int>(digitsStart + padding + k), .y = row}];
                cell.character = std::string(1, lineNumber[k]);
                lineNumberBrush.ApplyTo(cell);
            }
        }

        // Glyph -- the real gutter's own symbol column, one fixed column
        // (not staircased with depth -- the content text's own indent below
        // carries that cue instead), matching an ordinary content row's
        // symbol glyph exactly.
        if (static_cast<int>(symbolStart) < width) {
            const Brush glyphBrush{.background = theme_.tabBar.background,
                                   .foreground = theme_.BrushFor(editor::SyntaxClassFor(marker.kind)).foreground,
                                   .bold       = true};
            Cell&       glyphCell = c[{.x = static_cast<int>(symbolStart), .y = row}];
            glyphCell.character   = SymbolGlyphFor(marker.kind);
            glyphBrush.ApplyTo(glyphCell);
        }

        // sticky-scroll-signature follow-up: content area (starting at the
        // real gutterWidth, exactly where an ordinary row's own text
        // starts) shows a trimmed copy of the marker's own source line (its
        // "reduced signature") rather than just the bare identifier -- more
        // informative at a glance, and reads as real code (a template-param/
        // base-class list, return type, etc.) instead of a synthesized
        // label. Deliberately just the marker's own start LINE, verbatim
        // minus leading indentation -- not a joined/flattened multi-line
        // signature; "the line it's sticky on" is exactly that, one real
        // source line, same as every other sticky-scroll implementation
        // (VSCode/Zed) shows. Staircase-indented one level per ancestor row
        // -- the depth cue the glyph used to carry back when it lived here
        // instead of the real symbol column above.
        int col = static_cast<int>(gutterWidth) + static_cast<int>(i) * 2;
        if (col >= width) {
            continue;
        }
        const std::size_t lineStart = content.LineToByteOffset(line);
        const std::size_t lineEnd =
            (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        const std::string rawLine       = content.Substring(lineStart, lineEnd - lineStart);
        const std::size_t firstNonBlank = rawLine.find_first_not_of(" \t");
        const std::string trimmedLine   = (firstNonBlank == std::string::npos) ? std::string() : rawLine.substr(firstNonBlank);

        // Signature text keeps whatever brush the row's own blank fill above
        // already applied (theme_.tabBar) -- plain chrome-text color, not
        // colored/bolded by symbol kind; only cell.character changes here.
        // A source line's own text is real prose/code, not guaranteed ASCII
        // (a Markdown/Org heading's own text can hold an em-dash, curly
        // quote, accented name, ...) -- PaintUtf8Row (Border.h) steps by
        // codepoint rather than by raw byte, so a multi-byte codepoint lands
        // in exactly one cell instead of having its continuation bytes each
        // claim their own (invalid, unrenderable) cell -- confirmed live: an
        // em-dash in a heading rendered as a blank glyph under the old
        // byte-per-cell walk. Column count (for the truncation check below)
        // is therefore counted in codepoints, not bytes, for the same
        // reason.
        // Reserves the row's own last column for a "…" marker when the
        // trimmed line doesn't fit, rather than silently cutting off
        // mid-signature (ListPopup's own convention).
        std::size_t columnsNeeded = 0;
        for (std::size_t offset = 0; offset < trimmedLine.size(); ++columnsNeeded) {
            offset = text::NextCodepointBoundary(trimmedLine, offset);
        }
        const bool truncated = col + static_cast<int>(columnsNeeded) > width;
        const int  textLimit = truncated ? width - 1 : width;
        if (col < textLimit) {
            col += PaintUtf8Row(c, col, row, trimmedLine, theme_.tabBar, textLimit - col);
        }
        if (truncated && col < width) {
            c[{.x = col, .y = row}].character = "…";
        }
    }
    return static_cast<int>(chain.size());
}

void BufferView::Paint(Canvas paneCanvas) {
    viewport_.EnsureTopLineValidForActiveBuffer();
    EnsureStatusMessageFreshness();

    text::Buffer& buffer = activeBuffer_.Get();
    if (modeSyncBuffer_ != &buffer) {
        modeSyncBuffer_ = &buffer;
        if (onActiveBufferChanged_) {
            onActiveBufferChanged_(buffer);
        }
        // The callback just (possibly) replaced mode_ -- but a GutterWidth()
        // call during the switch's own event handling (ScrollToShowPoint,
        // CursorPosition) may already have run the mode-derived gutter
        // caches against this buffer under the OLD mode and stamped them
        // current-and-empty, which the (buffer, generation) gate can't
        // detect (neither stamp changes with the mode). Confirmed live via
        // an instrumented trace: switching back to a python buffer after
        // visiting a fundamental-mode one left the symbol/test columns
        // permanently blank until the next edit. Discarding the stamps here
        // forces one recompute under the mode that will actually paint.
        gutters_.InvalidateModeDependentCaches();
    }
    // Diff gutter markers follow-up: a newly-active buffer's diff markers
    // belong to a completely different file -- clearing immediately
    // (rather than leaving the old buffer's markers visible until the
    // fresh request completes) avoids a real, if brief, "wrong file's
    // markers" flash; RequestDiffForCurrentBuffer then kicks off a fresh,
    // unthrottled (not debounced -- switching buffers is a natural "want
    // it now" moment, same as a save) request for this buffer.
    //
    // initial-buffer-diff fix: tracked by its own diffSyncBuffer_, NOT
    // folded into the modeSyncBuffer_ branch above -- the constructor
    // deliberately pre-seeds modeSyncBuffer_ to suppress a spurious
    // first-frame onActiveBufferChanged_, and while the diff request lived
    // in that branch the seeding silently suppressed the initial buffer's
    // diff request too: the file ned was launched on (and every new split
    // pane's starting view) never showed markers until an edit/save
    // happened to fire a request. Confirmed against a live session with
    // gdb (RequestDiffForCurrentBuffer never called at all), not assumed.
    // diffSyncBuffer_ starts null instead, so a pane's very first Paint()
    // fetches its buffer's diff.
    if (diffSyncBuffer_ != &buffer) {
        diffSyncBuffer_ = &buffer;
        diffLineKinds_.clear();
        RequestDiffForCurrentBuffer();
    }

    const text::ITextStorage& content    = buffer.Content();
    const std::size_t         totalLines = content.LineCount();
    const Brush               emptyBrush = theme_.BrushFor(editor::SyntaxClass::Default);
    const std::size_t         point      = buffer.Point();
    const std::size_t         pointLine  = content.ByteOffsetToLine(point);

    // narrow-to-region/widen follow-up: caps which rows actually get
    // painted -- deliberately a *separate* value from totalLines above,
    // which stays the real, whole-buffer line count. lineEnd/
    // lineEndWithNewline below still need to know whether `line` is the
    // buffer's own real last line (to fall back to content.ByteLength())
    // regardless of narrowing -- the narrowed range's own last line is
    // usually not the buffer's real last line at all, just the last one
    // currently visible, and still needs its real next-line boundary looked
    // up correctly.
    const std::size_t renderEndLine = viewport_.NarrowedLineRange().second;

    if (scrollBar_ != nullptr) {
        // scrollable_length is fed as viewport_.MaxTopLine() + 1, not totalLines: the
        // scroll bar internally clamps a user-driven drag/click's target
        // position to [0, scrollable_length - 1], so this is what makes its
        // own built-in range match ours exactly -- dragging all the way
        // down actually reaches true end-of-file, not one line short of it.
        scrollBar_->scrollable_length  = static_cast<int>(viewport_.MaxTopLine()) + 1;
        scrollBar_->position           = static_cast<int>(viewport_.TopLine());
        scrollBar_->item_visual_length = 1; // one buffer line per canvas row
    }
    if (scrollUpArrow_ != nullptr) {
        scrollUpArrow_->SetEnabled(viewport_.TopLine() > 0);
    }
    if (scrollDownArrow_ != nullptr) {
        scrollDownArrow_->SetEnabled(viewport_.TopLine() < viewport_.MaxTopLine());
    }
    if (minimap_ != nullptr) {
        // Same fields, same semantics, same values ScrollBar's own sync
        // above uses -- Minimap mirrors ScrollBar's public surface exactly
        // so its viewport-band/click math stays consistent with it.
        minimap_->scrollable_length  = static_cast<int>(viewport_.MaxTopLine()) + 1;
        minimap_->position           = static_cast<int>(viewport_.TopLine());
        minimap_->item_visual_length = 1;
    }

    // Every column offset for this frame, from the same computation that
    // decides how wide the gutter is -- see BufferView/GutterLayout.h.
    const bufferview::GutterLayout gutter = ComputeGutterLayout(totalLines);

    std::vector<editor::dap::DapManager::Breakpoint>   dapBreakpoints;
    std::optional<std::pair<std::string, std::size_t>> dapStop;
    if (gutter.dapWidth > 0) {
        dapBreakpoints = dapManager_->BreakpointsForKey(dapPathKey_);
        dapStop        = dapManager_->CurrentStopKeyAndLine();
        if (dapStop && dapStop->first != dapPathKey_) {
            dapStop.reset(); // stopped in some other file -- nothing to mark here
        }
    }

    // status-gutter unsaved-change-indicator follow-up: recomputed once
    // per Paint() call (not per row) -- see EnsureUnsavedChangeCache's own
    // doc comment. Unconditional, unlike EnsureFoldGutterCache -- every
    // buffer gets a status column regardless of mode/language.
    const std::vector<std::pair<std::size_t, std::size_t>>& unsavedChangeLineRanges = gutters_.UnsavedChangeLineRanges();
    // LSP client follow-up: same "unconditional, every buffer gets one"
    // reasoning as EnsureUnsavedChangeCache above.
    const std::vector<std::pair<std::size_t, text::Buffer::Diagnostic::Severity>>& diagnosticLineSeverities =
        gutters_.DiagnosticLineSeverities();
    // VCS blame gutter: unconditional every Paint() like the two above, but
    // this only ever clears (never repopulates) blameLineInfo_ -- see its
    // own doc comment.
    EnsureBlameGutterCache();

    // LSP client follow-up: syncs the *active* buffer only, once per frame
    // -- see LspManager::SyncBuffer's own doc comment for why only the
    // currently-visible buffer, not every open one. A no-op if lspManager_
    // is unset (ordinary tests) or nothing's configured for this mode's
    // language (LspServerCommand returns nullopt, checked inside SyncBuffer
    // itself).
    if (lspManager_) {
        lspManager_->SyncBuffer(buffer, editor::LanguageKeyForMode(mode_));

        // Shared viewport-as-byte-range computation for both requests just
        // below -- unlike HugeStructuralWindow, no huge-buffer-only
        // parse-safety margin (there's no parser to desync here, just a
        // request scope).
        const std::size_t lastLine          = totalLines > 0 ? totalLines - 1 : 0;
        const std::size_t viewportTop       = std::min(viewport_.TopLine(), lastLine);
        const std::size_t viewportHeight    = size().height > 0 ? static_cast<std::size_t>(size().height) : 1;
        const std::size_t viewportBottom    = std::min(viewportTop + viewportHeight, lastLine);
        const std::size_t viewportStartByte = content.LineToByteOffset(viewportTop);
        const std::size_t viewportEndByte   = std::min(content.LineToByteOffset(viewportBottom) + 1, content.ByteLength());

        // semanticTokens follow-up, extended by the range/delta follow-up:
        // same per-frame, active-buffer-only cadence as SyncBuffer just
        // above, deliberately called from here (BufferView's own per-frame
        // decision point) rather than from inside LspManager::SyncToServer
        // -- see RequestSemanticTokens' own doc comment in LspManager.h for
        // why keeping it out of that hot path matters (a real lesson from
        // pull-diagnostics' own test-regression fix), and for how it
        // chooses among range/full-delta/full internally. No-ops
        // internally when disabled, unopened, or the exact same request
        // would already be in flight.
        lspManager_->RequestSemanticTokens(buffer, viewportStartByte, viewportEndByte, editor::LanguageKeyForMode(mode_));

        // inlayHint follow-up: same per-frame cadence as the calls above,
        // scoped to the currently visible line range -- unlike
        // semanticTokens' original whole-document request, inlayHint's own
        // "range" param exists specifically so a client only asks for
        // what's on screen.
        lspManager_->RequestInlayHints(buffer, viewportStartByte, viewportEndByte, editor::LanguageKeyForMode(mode_));

        // codeLens follow-up: same per-frame cadence as the calls above,
        // whole-document scope (codeLens has no "range" param, unlike
        // inlayHint) -- see RequestCodeLenses' own doc comment in
        // LspManager.h for the dedup/learn-once gating this no-ops behind.
        lspManager_->RequestCodeLenses(buffer, editor::LanguageKeyForMode(mode_));

        // embedded-language-documents follow-up: computes/caches this
        // buffer's embedded documents (currently just html-mode's
        // <script>/<style> content) once per actually-changed Paint() call
        // -- see EnsureEmbeddedDocumentCache -- then syncs each to its own
        // real LSP server. Called unconditionally, same as SyncBuffer just
        // above: cheap when nothing changed, gated internally by each
        // server's own lastSyncedGeneration, and also what tears down a
        // server whose only region was just edited away (an empty list here
        // when mode_.embeddedRegions is unset or reports nothing).
        std::vector<editor::lsp::LspManager::EmbeddedDocumentSync> embeddedSync;
        EnsureEmbeddedDocumentCache();
        if (const auto cacheIt = embeddedDocumentCacheByBuffer_.find(&buffer); cacheIt != embeddedDocumentCacheByBuffer_.end()) {
            embeddedSync.reserve(cacheIt->second.documents.size());
            for (const editor::EmbeddedDocument& document : cacheIt->second.documents) {
                embeddedSync.push_back(editor::lsp::LspManager::EmbeddedDocumentSync{
                    .language = document.language, .documentText = document.documentText, .ownedRanges = document.ownedRanges});
            }
        }
        lspManager_->SyncEmbeddedDocuments(buffer, embeddedSync);
    }

    // error-visibility follow-up: a cheap once-per-frame poll, the same
    // "recompute, don't cache" idiom every other Paint()-time check here
    // already uses. Gated on statusMessage_ being empty so this never
    // clobbers an in-progress prompt or another just-set message -- surface
    // via statusMessage_, never forcibly switch the user's buffer out from
    // under them (see BufferView::StartInteractiveSession's LspShowLog case
    // for the actual, user-initiated way to view the log).
    if (lspManager_ && lspManager_->HasUnseenLogEntry() && statusMessage_.empty()) {
        statusMessage_ = "LSP error -- see *lsp log* (M-x lsp-show-log)";
        lspManager_->AcknowledgeLogEntry();
    }

    // user-facing-hang-affordance follow-up (ChildProcess-hang-protection-
    // round-2): same once-per-frame poll idiom as the LSP-log check above,
    // just against the shared *Messages* log instead of the older, LSP-only
    // "*lsp log*" one -- a hang/timeout recovery (a killed clipboard-paste
    // subprocess, an LSP/DAP/ACP stall disconnect, a stale-request timeout)
    // previously left no trace beyond that log entry itself, with nothing to
    // draw the user's eye to it. Checked after the LSP-log branch above so
    // the two never race for the same frame's statusMessage_ (whichever
    // fires first wins; the other's flag stays set and surfaces next frame
    // once statusMessage_ is empty again). Gated on surfaceUnseenLogEntries_
    // (default false, see SetSurfaceUnseenLogEntries's own doc comment) --
    // unlike lspManager_ above, editor::HasUnseenDiagnosticsLogEntry() reads
    // genuinely process-wide state, so an unconditional check here would let
    // any other test's own LogMessage call leak into this one's Paint().
    if (surfaceUnseenLogEntries_ && editor::HasUnseenDiagnosticsLogEntry() && statusMessage_.empty()) {
        statusMessage_ = "New warning -- see *Messages* (M-x show-messages)";
        editor::AcknowledgeDiagnosticsLogEntry();
    }

    // diagnostics-UX follow-up: live echo of the diagnostic on point's own
    // line, updating as point moves, so reading an error never requires a
    // command at all (lsp-show-diagnostic stays for the full/multi-message
    // case). Same once-per-frame poll idiom as the LSP-log check above.
    // autoDiagnosticMessage_ remembers exactly what this poll last wrote so
    // it only ever overwrites/clears its OWN message -- a real command
    // result, prompt text, or any other writer always wins, and leaving the
    // line takes the echo away instead of it lingering like a normal status
    // message would.
    if (inputMode_ == InputMode::Normal) {
        const std::size_t pointLineStart = content.LineToByteOffset(pointLine);
        const std::size_t pointLineEnd =
            (pointLine + 1 < content.LineCount()) ? content.LineToByteOffset(pointLine + 1) : content.ByteLength();
        const text::Buffer::Diagnostic* firstOnLine = nullptr;
        std::size_t                     extraOnLine = 0;
        for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
            if (diagnostic.startByte >= pointLineStart && diagnostic.startByte < pointLineEnd) {
                if (firstOnLine == nullptr) {
                    firstOnLine = &diagnostic;
                }
                else {
                    ++extraOnLine;
                }
            }
        }
        std::string autoMessage;
        if (firstOnLine != nullptr) {
            switch (firstOnLine->severity) {
                case text::Buffer::Diagnostic::Severity::Error:
                    autoMessage = "Error: ";
                    break;
                case text::Buffer::Diagnostic::Severity::Warning:
                    autoMessage = "Warning: ";
                    break;
                case text::Buffer::Diagnostic::Severity::Information:
                    autoMessage = "Info: ";
                    break;
                case text::Buffer::Diagnostic::Severity::Hint:
                    autoMessage = "Hint: ";
                    break;
            }
            autoMessage += firstOnLine->message;
            if (extraOnLine > 0) {
                autoMessage += " (+" + std::to_string(extraOnLine) + " more on this line)";
            }
        }
        if (!autoMessage.empty()) {
            if (statusMessage_.empty() || statusMessage_ == autoDiagnosticMessage_) {
                statusMessage_         = autoMessage;
                autoDiagnosticMessage_ = autoMessage;
            }
        }
        else if (!autoDiagnosticMessage_.empty()) {
            if (statusMessage_ == autoDiagnosticMessage_) {
                statusMessage_.clear();
            }
            autoDiagnosticMessage_.clear();
        }
    }

    // depth-aware-fold-gutter follow-up: recomputed once per Paint() call
    // (not per row, and not rebuilt from scratch even across separate
    // Paint() calls when neither content nor fold state has changed) -- see
    // EnsureFoldGutterCache's/gutters_.FoldEntries()'s own doc comments in
    // BufferView.h for the real [Performance]-test-driven history behind
    // exactly what's cached here and why (a naive per-row scan, then an
    // unordered_map that measurably made ASan wall time *worse* via its
    // per-insert heap allocations, then finally this: sorted vectors cached
    // alongside gutters_.FoldableBlocks() itself rather than rebuilt every
    // Paint() call).

    // Recomputed only when the active buffer or its content has actually
    // changed since the last Paint() call -- see highlightCacheBuffer_'s own
    // doc comment in BufferView.h for why this caching exists at all (a real,
    // measured perf fix, not a preemptive one).
    // read-only-buffers follow-up: a synthesized, read-only buffer (project-
    // search results, project-replace's preview, project-agenda) is never
    // real code in whatever language the pane's own Mode happens to be --
    // running that Mode's highlight query against "path:line: text" content
    // would produce meaningless spans, not an empty result, so ReadOnly()
    // suppresses this the same way gutters_.FoldGutterActive() suppresses folding.
    // Past editor::MaxHighlightBytes() (loose-ends follow-up: was a
    // hardcoded 8 MiB kMaxHighlightBytes here, now Editor/
    // HighlightSettings.h's configurable process-wide setting), never run
    // mode_.highlight at all, regardless of whether the file's extension
    // matches a real grammar -- buffer.ReadOnly() already suppresses
    // highlighting for results buffers and IsLoading() placeholders, but a
    // huge file that finishes loading and reverts to writable would
    // otherwise still pay a full buffer.Text() copy plus a whole-buffer
    // tree-sitter parse on every edit.
    // Written once and used for both the check and the store, so the two
    // cannot drift; a lambda rather than a plain local because the semantic
    // tokens generation must stay lazily evaluated in the else-if below.
    const auto highlightStampFor = [&](std::size_t semanticTokensGeneration) {
        return bufferview::CacheStamp::For(
            &buffer, {buffer.ContentGeneration(), editor::CaptureClassGeneration(), semanticTokensGeneration});
    };
    if (!mode_.highlight || buffer.ReadOnly() || buffer.Size() > editor::MaxHighlightBytes()) {
        highlightCacheStamp_.Invalidate();
        highlightCacheSpans_.clear();
    }
    else if (const std::size_t semanticTokensGeneration = lspManager_ ? lspManager_->SemanticTokensGeneration(buffer) : 0;
             !highlightCacheStamp_.Matches(highlightStampFor(semanticTokensGeneration))) {
        // per-buffer-highlight-cache follow-up: persists across a buffer
        // switch, not just repeated Paint() calls on the same buffer -- see
        // highlightCacheByBuffer_'s own doc comment in BufferView.h. The
        // CaptureClassGeneration() check (exhaustive-highlighting
        // follow-up) and the modeName check (this follow-up) both matter
        // here for the same reason: either can make a stale entry's spans
        // wrong even though buffer's content hasn't changed at all --
        // semanticTokensGeneration (semanticTokens follow-up) is the same
        // idea again: an LSP response can arrive, and change what should
        // render, with no buffer edit at all.
        const auto it = highlightCacheByBuffer_.find(&buffer);
        if (it == highlightCacheByBuffer_.end() || it->second.contentGeneration != buffer.ContentGeneration() ||
            it->second.classGeneration != editor::CaptureClassGeneration() || it->second.modeName != mode_.name ||
            it->second.semanticTokensGeneration != semanticTokensGeneration) {
            HighlightCacheEntry entry;
            entry.spans = mode_.highlight(buffer.Text());
            // semanticTokens follow-up: appended *after* tree-sitter's own
            // spans so LSP-informed classification wins at overlapping
            // bytes -- the exact "later span wins" convention the
            // injection engine already exploits for the same reason (see
            // Mode.h's own HighlightSpan doc comment), not a new
            // resolution rule. Empty when lspManager_ is unset, disabled,
            // or no response has landed yet.
            if (lspManager_) {
                const std::vector<editor::HighlightSpan>& semanticSpans = lspManager_->SemanticTokenSpans(buffer);
                entry.spans.insert(entry.spans.end(), semanticSpans.begin(), semanticSpans.end());
            }
            entry.contentGeneration        = buffer.ContentGeneration();
            entry.classGeneration          = editor::CaptureClassGeneration();
            entry.semanticTokensGeneration = semanticTokensGeneration;
            entry.modeName                 = mode_.name;
            highlightCacheSpans_           = entry.spans;
            highlightCacheByBuffer_.insert_or_assign(&buffer, std::move(entry));
        }
        else {
            highlightCacheSpans_ = it->second.spans;
        }
        highlightCacheStamp_ = highlightStampFor(semanticTokensGeneration);
    }
    const std::vector<editor::HighlightSpan>& highlightSpans = highlightCacheSpans_;

    // Links follow-up: see EnsureLinkCache's own doc comment in BufferView.h
    // for why this is a no-op outside an org-mode buffer.

    // depth-aware-fold-gutter follow-up: streaming state for the per-row
    // gutter rendering below -- one pass over the whole row loop, not
    // rebuilt per row; see that code's own doc comment for why a plain
    // per-column stack is the correct (and correctly performing) structure
    // here. Plain Paint()-local state, reset fresh every call.
    std::size_t                                                foldGutterEntryCursor = 0;
    std::array<const FoldGutterEntry*, kMaxFoldDepthColumns>   foldGutterHeaderAtColumn{};
    std::array<std::size_t, kMaxFoldDepthColumns>              foldColumnCursor{};
    std::array<std::vector<std::size_t>, kMaxFoldDepthColumns> foldColumnOpenEnds;

    // A running buffer-line cursor, seeded at viewport_.TopLine() (already guaranteed
    // visible by SetTopLine) and advanced by NextVisibleLine each iteration
    // -- Org-mode fold/unfold follow-up: was a flat `viewport_.TopLine() + row` 1:1
    // mapping; a fold can make "the row-th line below viewport_.TopLine()" and "the
    // row-th buffer line below viewport_.TopLine()" disagree, so this has to walk
    // forward skipping whatever's currently hidden instead.
    std::size_t line = viewport_.TopLine();
    // line-wrap follow-up: segmentIndex is which wrap segment (row) of
    // `line` is currently being drawn -- 0 for a non-wrapped line, always.
    // lineSegments/currentLineSpans/currentLineLinks are recomputed only
    // when segmentIndex == 0 (i.e. this row starts a new buffer line), then
    // read on every row -- including continuation rows -- of that same
    // line, the same "compute once per line, not once per row" shape this
    // function already used for lineSpans/lineLinks before wrap existed.
    const bool                         wrapActive   = viewport_.EffectiveWrapLines();
    std::size_t                        segmentIndex = 0;
    std::vector<WrapSegment>           lineSegments;
    std::vector<editor::HighlightSpan> currentLineSpans;
    std::vector<RenderedLink>          currentLineLinks;
    std::vector<RenderedInlayHint>     currentLineInlayHints;
    // Whitespace-visualization follow-up: same "compute once per line, not
    // once per row/character" shape as currentLineSpans/currentLineLinks
    // above. currentLineTrailingWhitespaceStart is the byte offset of the
    // first byte in the line's own trailing run of spaces/tabs (lineEnd
    // itself if the line has no trailing whitespace, so the `offset >=`
    // check below is trivially false); currentLineIndentEnd is the byte
    // offset one past the line's own leading run of spaces/tabs (lineStart
    // itself if the line has no leading whitespace at all).
    std::size_t currentLineTrailingWhitespaceStart = 0;
    std::size_t currentLineIndentEnd               = 0;
    // Diff gutter markers follow-up: same "compute once per line, not once
    // per row/character" shape as currentLineSpans/currentLineLinks above --
    // feeds the subtle background tint applied per character below
    // (Removed has no line to tint, only Added/Modified ever populate this).
    std::optional<DiffLineKind> currentLineDiffTint;
    // Multibuffers follow-up: same "compute once per line" shape as
    // currentLineDiffTint above, but for the *vcs diff* multibuffer's own
    // stitched added/removed lines (a static property of that buffer's
    // content, not a live comparison against disk the way the source-file
    // diff gutter is) -- see the per-character brush selection below for
    // why a content-area wash is safe here specifically.
    std::optional<editor::multibuffer::LineTint> currentMultibufferTint;
    // diagnostics-UX follow-up: the diagnostic byte spans overlapping the
    // current line, feeding the per-character underline below -- same
    // "compute once per line" shape as everything above. A zero-length
    // diagnostic span (some servers report those) is widened to one byte so
    // it still underlines the cell it points at instead of vanishing.
    std::vector<std::pair<std::size_t, std::size_t>> currentLineDiagnosticSpans;
    // documentHighlight follow-up: the LSP-reported occurrence-of-symbol-at-
    // point byte spans overlapping the current line -- same "compute once
    // per line, from BufferView-owned ephemeral state" shape as
    // currentLineDiagnosticSpans above, but sourced from documentHighlight_
    // (a point-triggered request/response, not Buffer::Diagnostics()'
    // server-pushed set).
    std::vector<std::pair<std::size_t, std::size_t>> currentLineDocumentHighlightSpans;
    // DAP client slice 2: whether the debuggee is stopped exactly on this
    // line -- feeds both the whole-line background wash below and the
    // gutter arrow, so the two can never disagree.
    bool currentLineIsExecutionLine = false;
    // inline-diagnostics follow-up: set when the just-finished line carries
    // an annotation -- the NEXT loop iteration renders that annotation row
    // instead of a buffer line, mirroring how RowsForLine already counts it.
    std::optional<std::size_t> pendingAnnotationLine;
    // codeLens follow-up: the last line whose leading row has already been
    // painted -- unlike pendingAnnotationLine (an optional consumed the
    // very next iteration), a leading row must be checked/emitted the
    // FIRST time this loop reaches a line (segmentIndex == 0), before that
    // line's own real content, and must not re-trigger on that same
    // line's later wrap-continuation rows -- this sentinel is what tells
    // the two apart. kNoRowLine (never a real line index) starts it "no
    // line's leading row emitted yet."
    std::size_t leadingAnnotationEmittedLine = kNoRowLine;
    // prose-diagnostic-callout follow-up: recorded per screen row as the main
    // loop below paints it, then walked in one pass by
    // PaintProseDiagnosticCallouts after the loop -- kNoRowLine marks a row
    // that isn't showing any real buffer line content (an annotation row, or
    // a blank trailing row past end-of-buffer), so a callout brace never
    // mistakes one for open space or mistakes it for part of a line's own
    // span. rowContentEndColumn defaults to "fully blocked" (the row's own
    // width) for exactly that reason -- an unrecognized row must never look
    // like free room; the two branches below that leave a row's `line`
    // untouched (the annotation-row continue, and the past-end-of-buffer
    // blank-row case) explicitly correct that default where it's actually
    // known to be safe.
    // main-editor-sticky-scroll follow-up: drawn into paneCanvas (the pane's
    // full, unshifted view) first; every line of code below this point (the
    // row loop, its rowLine/rowContentEndColumn bookkeeping,
    // PaintProseDiagnosticCallouts) then works against `c`, a NEW Canvas
    // bound to a Box shifted down by however many rows were just drawn --
    // Canvas has a reference member (deleted copy-assignment), so this has
    // to be a fresh local binding, not a reassignment of paneCanvas itself;
    // that's the whole reason the parameter is named paneCanvas and not `c`
    // here. A 0-row shift is a true no-op, so this needs no branch for the
    // common "nothing pinned" case. stickyRowCount_ itself is live
    // BufferView state (not Paint()-local) -- CursorPosition()/
    // viewport_.ByteOffsetForPoint() read it back so the terminal cursor, popup
    // anchors, and mouse-click row resolution all agree with what got drawn
    // this frame.
    stickyRowCount_ = PaintStickyScrollRows(paneCanvas, gutter.totalWidth);
    Box shiftedBox  = Box_();
    shiftedBox.y_min += stickyRowCount_;
    Canvas c = paneCanvas.ForBox(shiftedBox);

    std::vector<std::size_t> rowLine(static_cast<std::size_t>(c.size().height), kNoRowLine);
    std::vector<int>         rowContentEndColumn(static_cast<std::size_t>(c.size().height), c.size().width);
    for (int row = 0; row < c.size().height; ++row) {
        for (int col = 0; col < c.size().width; ++col) {
            Cell& cell     = c[{.x = col, .y = row}];
            cell.character = " ";
            emptyBrush.ApplyTo(cell);
        }

        if (pendingAnnotationLine) {
            PaintInlineDiagnosticRow(c, row, *pendingAnnotationLine, gutter.totalWidth);
            pendingAnnotationLine.reset();
            continue; // consumed this row; `line` already points at the next buffer line
        }

        // codeLens follow-up: checked/emitted the first time this loop
        // reaches `line` (segmentIndex == 0), BEFORE that line's own real
        // content -- the opposite ordering from pendingAnnotationLine's
        // trailing row above. Neither `line` nor `segmentIndex` advance
        // here, so the very next iteration renders this same line's real
        // first row normally; leadingAnnotationEmittedLine is what stops
        // this branch from re-triggering on that next iteration.
        if (line < renderEndLine && segmentIndex == 0 && line != leadingAnnotationEmittedLine &&
            LeadingAnnotationRowsForLine(line) > 0) {
            PaintCodeLensRow(c, row, line, gutter.totalWidth);
            leadingAnnotationEmittedLine = line;
            continue;
        }

        if (line < renderEndLine) {
            const std::size_t lineStart = content.LineToByteOffset(line);
            const std::size_t lineEnd =
                (line + 1 < totalLines) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();

            // line-wrap follow-up: recomputed only when this row starts a
            // new buffer line (segmentIndex == 0), then read on every row
            // of that same line, including continuation rows -- the same
            // "compute once per line" shape lineSpans/lineLinks already
            // used before wrap existed, now also covering lineSegments
            // itself. A non-wrapped line always gets exactly one segment
            // spanning its whole content, so every call site below that
            // reads lineSegments[segmentIndex] behaves identically to the
            // pre-wrap code when wrapActive is false.
            if (segmentIndex == 0) {
                currentLineSpans = SpansForLine(highlightSpans, lineStart, lineEnd);
                currentLineLinks = LinksForLine(viewport_.Links(), lineStart, lineEnd, point);
                // inlayHint follow-up: empty when lspManager_ is unset,
                // disabled, or no response has landed for this line's range
                // yet -- InlayHintSpans itself is O(1) (no cache to poll a
                // generation counter for), so no extra staleness bookkeeping
                // is needed here unlike the tree-sitter highlight cache.
                currentLineInlayHints =
                    InlayHintsForLine(lspManager_ ? lspManager_->InlayHintSpans(buffer) : std::vector<editor::lsp::LspManager::ResolvedInlayHint>{},
                                      lineStart, lineEnd);
                // Whitespace-visualization follow-up: skipped (both fields
                // left at their "empty run" default) unless at least one of
                // the two features is on, so a default-off installation pays
                // no extra Substring-per-line cost here.
                currentLineTrailingWhitespaceStart = lineEnd;
                currentLineIndentEnd               = lineStart;
                if (editor::TrailingWhitespaceHighlightEnabled() || editor::IndentGuidesEnabled()) {
                    const std::string lineText      = content.Substring(lineStart, lineEnd - lineStart);
                    std::size_t       trailingStart = lineText.size();
                    while (trailingStart > 0 &&
                           (lineText[trailingStart - 1] == ' ' || lineText[trailingStart - 1] == '\t')) {
                        --trailingStart;
                    }
                    currentLineTrailingWhitespaceStart = lineStart + trailingStart;

                    std::size_t indentEnd = 0;
                    while (indentEnd < lineText.size() && (lineText[indentEnd] == ' ' || lineText[indentEnd] == '\t')) {
                        ++indentEnd;
                    }
                    currentLineIndentEnd = lineStart + indentEnd;
                }
                currentLineDiagnosticSpans.clear();
                for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
                    // prose-diagnostic-callout follow-up: no code-style
                    // underline for the prose/grammar checker's own
                    // diagnostics -- see PaintProseDiagnosticCallouts.
                    if (diagnostic.origin != text::Buffer::Diagnostic::Origin::Code) {
                        continue;
                    }
                    const std::size_t spanEnd = std::max(diagnostic.endByte, diagnostic.startByte + 1);
                    if (diagnostic.startByte < lineEnd && spanEnd > lineStart) {
                        currentLineDiagnosticSpans.emplace_back(diagnostic.startByte, spanEnd);
                    }
                }
                currentLineDocumentHighlightSpans.clear();
                if (documentHighlight_ && documentHighlight_->buffer == &buffer &&
                    documentHighlight_->contentGeneration == buffer.ContentGeneration()) {
                    for (const auto& [start, end] : documentHighlight_->ranges) {
                        if (start < lineEnd && end > lineStart) {
                            currentLineDocumentHighlightSpans.emplace_back(start, end);
                        }
                    }
                }
                currentLineIsExecutionLine = dapStop && dapStop->second == line + 1; // dapStop already file-filtered above
                currentLineDiffTint.reset();
                if (gutter.diffWidth > 0) {
                    const auto diffIt = std::lower_bound(diffLineKinds_.begin(), diffLineKinds_.end(), line,
                                                         [](const auto& entry, std::size_t targetLine) { return entry.first < targetLine; });
                    if (diffIt != diffLineKinds_.end() && diffIt->first == line && diffIt->second != DiffLineKind::Removed) {
                        currentLineDiffTint = diffIt->second;
                    }
                }
                currentMultibufferTint.reset();
                if (const auto* multibufferIndex = editor::multibuffer::MultibufferIndexFor(buffer)) {
                    if (const auto tint = multibufferIndex->TintForLine(line); tint != editor::multibuffer::LineTint::None) {
                        currentMultibufferTint = tint;
                    }
                }
                if (wrapActive) {
                    const int fullWidth = std::max(1, c.size().width - static_cast<int>(gutter.totalWidth));
                    lineSegments        = ComputeWrappedLineSegments(content, lineStart, lineEnd, fullWidth, currentLineLinks);
                }
                else {
                    lineSegments = {WrapSegment{.startByte = lineStart, .endByte = lineEnd}};
                }
            }
            const WrapSegment& currentSegment = lineSegments[segmentIndex];

            // line-wrap follow-up: everything in this block is per-REAL-LINE,
            // not per-row (a line number/fold glyph only ever belongs on a
            // line's own first row) -- skipped entirely for a continuation
            // row of a wrapped line; the top-of-row blanking pass already
            // washed this row's gutter columns blank.
            if (segmentIndex == 0) {
                // Includes the line's own newline (unlike lineEnd above), so a
                // region selected through to the start of the next line still
                // counts this one as fully selected -- see ClassifyGutterSelection.
                const std::size_t     lineEndWithNewline = (line + 1 < totalLines) ? content.LineToByteOffset(line + 1) : content.ByteLength();
                const GutterSelection gutterSelection    = ClassifyGutterSelection(buffer, lineStart, lineEndWithNewline);

                // Diff gutter markers follow-up: a changed line's own
                // number gets colored toward the accent instead of the
                // usual line-number foreground -- real visual signal
                // without ever touching the code text's own contrast
                // (revised away from a whole-line background wash, which
                // by definition fights contrast against similarly-hued
                // foreground text; a user-reported "wipes out the text"
                // complaint against exactly that approach is what drove
                // this). currentLineDiffTint is only ever set for
                // Added/Modified (never Removed -- see where it's
                // computed just above), matching the diff gutter column's
                // own choice to give Removed a distinct glyph instead.
                const Color gutterForeground = currentLineDiffTint
                                                   ? (currentLineDiffTint == DiffLineKind::Added ? Color::BrightGreen : Color::BrightBlue)
                                               : (line == pointLine) ? theme_.currentLineNumberForeground
                                                                     : theme_.lineNumberForeground;
                // Digits+padding get the full selection background only when the
                // whole line is covered; the one-column gap after them gets it for
                // Partial too, so a partially-selected line still shows a thin
                // highlighted edge instead of no indication at all.
                const Brush gutterBrush{
                    .background = (gutterSelection == GutterSelection::Full) ? theme_.selectionBackground : theme_.background,
                    .foreground = gutterForeground,
                };
                const Brush gutterGapBrush{
                    .background = (gutterSelection != GutterSelection::None) ? theme_.selectionBackground : theme_.background,
                    .foreground = gutterForeground,
                };
                // DAP client slice 2/4: the debug-marker column -- an
                // execution arrow where the debuggee is stopped (winning
                // over a breakpoint marker on the same line: "you are
                // here" beats "you asked to stop here"), else a glyph by
                // breakpoint kind (plain/conditional/hit-count/logpoint)
                // colored by verified state. Same plain-single-width-Unicode
                // discipline as the diagnostic glyphs (▸ is the sidebar's
                // own proven disclosure triangle; ●/◆/◇/○ are from the same
                // geometric-shapes range as the scroll arrows -- DAP round 3
                // adds ◇, the open-diamond hit-count sibling of ◆'s filled
                // condition glyph).
                if (gutter.dapWidth > 0) {
                    Cell& cell = c[{.x = 0, .y = row}];
                    if (currentLineIsExecutionLine) {
                        cell.character = "▸";
                        Brush{.background = theme_.background, .foreground = theme_.executionMarker, .bold = true}.ApplyTo(cell);
                    }
                    else {
                        // DAP round 4: dapBreakpoints stays sorted by the
                        // *requested* line (toggle/condition/logMessage/
                        // hitCondition all still address that) -- so a
                        // linear scan on the *display* line (actualLine when
                        // the adapter snapped it elsewhere, else line) is
                        // what actually shows a moved breakpoint where it
                        // really lands, rather than where it was toggled.
                        // Per-file breakpoint counts are small; a lower_bound
                        // can't be reused once the sort key and lookup key
                        // diverge like this.
                        const auto bpIt = std::find_if(dapBreakpoints.begin(), dapBreakpoints.end(),
                                                       [line](const editor::dap::DapManager::Breakpoint& bp) {
                                                           return (bp.actualLine != 0 ? bp.actualLine : bp.line) == line + 1;
                                                       });
                        if (bpIt != dapBreakpoints.end()) {
                            cell.character    = !bpIt->logMessage.empty()     ? "○"
                                                : !bpIt->condition.empty()    ? "◆"
                                                : !bpIt->hitCondition.empty() ? "◇"
                                                                              : "●";
                            const Color color = bpIt->verified ? theme_.breakpointMarker : theme_.unverifiedBreakpointMarker;
                            Brush{.background = theme_.background, .foreground = color}.ApplyTo(cell);
                        }
                    }
                }

                // Diff gutter markers follow-up: leftmost of the non-debug
                // regions, matching real editors' own git-gutter placement.
                // Direct Color constants, not routed through
                // Theme::BrushFor(SyntaxClass) -- same bypass the blame
                // gutter's own hash coloring already uses, for the same
                // reason (this isn't a tree-sitter capture category).
                // Drawn at gutter.diffStart -- the diff column's own x. This used
                // to (wrongly) target gutter.statusStart, where the unsaved-change
                // swatch below then unconditionally overwrote it every
                // frame, leaving the reserved diff column permanently
                // blank; found while adding the debug column and fixed on
                // request rather than silently, since the visible diff
                // styling (colored line numbers + content gradient) had
                // been tuned with the swatch invisibly absent.
                if (gutter.diffWidth > 0) {
                    const auto it = std::lower_bound(diffLineKinds_.begin(), diffLineKinds_.end(), line,
                                                     [](const auto& entry, std::size_t targetLine) { return entry.first < targetLine; });
                    if (it != diffLineKinds_.end() && it->first == line) {
                        // diff-gutter-icons follow-up (was a solid color
                        // swatch for Added/Modified): vim-gitgutter's own
                        // classic glyph vocabulary -- the shape says WHAT
                        // changed, not just that something did, same
                        // reasoning as the diagnostic column's severity
                        // icons. ▔ stays for a deletion: it's already
                        // iconographic (the notch marks where the deleted
                        // lines sat, at this line's own top edge).
                        Cell& cell            = c[{.x = static_cast<int>(gutter.diffStart), .y = row}];
                        cell.background_color = theme_.background;
                        cell.bold             = true;
                        switch (it->second) {
                            case DiffLineKind::Added:
                                cell.character        = "+";
                                cell.foreground_color = Color::BrightGreen;
                                break;
                            case DiffLineKind::Modified:
                                cell.character        = "~";
                                cell.foreground_color = Color::BrightBlue;
                                break;
                            case DiffLineKind::Removed:
                                cell.character        = "▔"; // UPPER ONE EIGHTH BLOCK
                                cell.foreground_color = Color::BrightRed;
                                break;
                        }
                    }
                }

                // status-gutter unsaved-change-indicator follow-up: a solid
                // colored cell (character " ", not a glyph -- a 1-char-wide
                // color swatch, matching the user's own "just 1 char width"
                // ask and ScrollBar's own thumb-via-cell.inverted precedent)
                // when this line has edits since the buffer was last
                // loaded/saved. A plain binary search against
                // unsavedChangeLineRanges_ -- these ranges are flat and
                // disjoint by construction, unlike the fold depth columns, so
                // no streaming stack state is needed here.
                {
                    const auto it = std::lower_bound(
                        unsavedChangeLineRanges.begin(), unsavedChangeLineRanges.end(), line,
                        [](const auto& range, std::size_t targetLine) { return range.second <= targetLine; });
                    const bool  changed        = it != unsavedChangeLineRanges.end() && it->first <= line;
                    const Color indicatorColor = changed ? theme_.unsavedChangeIndicator : theme_.background;
                    const Brush statusBrush{.background = indicatorColor, .foreground = indicatorColor};
                    Cell&       cell = c[{.x = static_cast<int>(gutter.statusStart), .y = row}];
                    cell.character   = " ";
                    statusBrush.ApplyTo(cell);
                }

                // LSP client follow-up (was a solid color swatch like the
                // status column just above; diagnostic-gutter-icons follow-up
                // made it a real glyph): a severity-specific icon in the
                // severity's theme color, so the column says what KIND of
                // diagnostic a line has, not just that one exists. Glyphs are
                // deliberately plain single-width Unicode, not Nerd Font
                // icons or emoji -- same portability/column-math reasoning
                // ProjectSidebar's own glyph-choice comment documents; every
                // pick is from a range this codebase already renders
                // single-width somewhere (geometric shapes: ScrollArrowButton's
                // own arrows; dingbat/ASCII/Latin-1: TabBar's close icon, the
                // gutter digits themselves). See diagnosticLineSeverities_'s
                // own doc comment for why a plain binary search suffices here
                // (at most one entry per line -- the most severe -- already
                // sorted).
                if (static_cast<int>(gutter.diagnosticStart) < c.size().width) {
                    const auto it            = std::lower_bound(diagnosticLineSeverities.begin(), diagnosticLineSeverities.end(), line,
                                                                [](const auto& entry, std::size_t targetLine) { return entry.first < targetLine; });
                    const bool hasDiagnostic = it != diagnosticLineSeverities.end() && it->first == line;
                    Cell&      cell          = c[{.x = static_cast<int>(gutter.diagnosticStart), .y = row}];
                    if (!hasDiagnostic) {
                        cell.character = " ";
                        Brush{.background = theme_.background, .foreground = theme_.background}.ApplyTo(cell);
                    }
                    else {
                        // Glyph choice shared with the inline annotation
                        // rows via DiagnosticGlyphFor -- see its own doc
                        // comment (was an inline switch here).
                        const DiagnosticGlyph glyph = DiagnosticGlyphFor(it->second);
                        cell.character              = glyph.glyph;
                        Brush{.background = theme_.background, .foreground = DiagnosticSeverityColor(theme_, it->second), .bold = glyph.bold}
                            .ApplyTo(cell);
                    }
                }

                // Multibuffers follow-up: entirely skipped (not just
                // zero-width) for a buffer whose own composite line numbers
                // would be meaningless noise next to the dual old/new
                // columns already baked into a *vcs diff*-style excerpt's
                // own text -- see LineNumberGutterActive()'s own doc
                // comment. gutter.digits/gutter.digitsStart are already computed
                // as 0-width/collapsed in that case (see this function's
                // own gutter.digits/gutter.digitsStart derivation above), but the
                // digit string itself (line + 1) is never zero-width, so
                // the write loop below has to be skipped outright rather
                // than trusted to naturally emit nothing.
                // trailing-blank-line-gutter follow-up: line totalLines-1
                // being empty, with more than one line total, means it
                // exists purely because the buffer's own final byte is a
                // newline (ITextStorage::LineCount()'s own "newline count +
                // 1" contract) -- not a line anyone ever typed into.
                // Numbering it like a real line is misleading, so it stays
                // unnumbered until it actually holds content (typing into
                // it makes lineStart != lineEnd, and it renders normally
                // from that point on). A brand new, genuinely empty buffer
                // (totalLines == 1) is excluded -- that lone line is real
                // and still gets "1".
                const bool isEmptyTrailingPhantomLine = line + 1 == totalLines && lineStart == lineEnd && totalLines > 1;
                if (LineNumberGutterActive() && !isEmptyTrailingPhantomLine) {
                    // Vim's "relativenumber": current line keeps its real
                    // (1-indexed) number, every other visible line shows its
                    // distance from it instead.
                    const std::string number  = editor::RelativeLineNumbersEnabled() && line != pointLine
                                                    ? std::to_string(line > pointLine ? line - pointLine : pointLine - line)
                                                    : std::to_string(line + 1); // 1-indexed, matches ModeLine's L/C convention
                    const std::size_t padding = gutter.digits > number.size() ? gutter.digits - number.size() : 0;
                    // Leading gap (status/line-number-spacing follow-up -- the
                    // line-number gutter now gets breathing room on BOTH sides,
                    // not just the trailing gap it already had). Sits right after
                    // the diagnostic column now (LSP client follow-up), not
                    // directly after the status column -- gutter.digitsStart itself
                    // already accounts for kDiagnosticWidth, so this is just
                    // "one column before gutter.digitsStart."
                    if (static_cast<int>(gutter.digitsStart - kLineNumberGap) < c.size().width) {
                        Cell& cell     = c[{.x = static_cast<int>(gutter.digitsStart - kLineNumberGap), .y = row}];
                        cell.character = " ";
                        gutterGapBrush.ApplyTo(cell);
                    }
                    for (std::size_t i = 0; i < padding && static_cast<int>(gutter.digitsStart + i) < c.size().width; ++i) {
                        Cell& cell     = c[{.x = static_cast<int>(gutter.digitsStart + i), .y = row}];
                        cell.character = " ";
                        gutterBrush.ApplyTo(cell);
                    }
                    for (std::size_t i = 0; i < number.size() && static_cast<int>(gutter.digitsStart + padding + i) < c.size().width; ++i) {
                        Cell& cell     = c[{.x = static_cast<int>(gutter.digitsStart + padding + i), .y = row}];
                        cell.character = std::string(1, number[i]);
                        gutterBrush.ApplyTo(cell);
                    }
                    if (static_cast<int>(gutter.digitsStart + gutter.digits) < c.size().width) {
                        Cell& cell     = c[{.x = static_cast<int>(gutter.digitsStart + gutter.digits), .y = row}];
                        cell.character = " ";
                        gutterGapBrush.ApplyTo(cell);
                    }
                }

                // depth-aware-fold-gutter follow-up: one column per nesting
                // level (capped at kMaxFoldDepthColumns) -- a block's OWN
                // header row shows its ⊞/⊟ toggle at its own column (⊟, real
                // Org's own "there's more, click to open" shape inverted --
                // classic outline-widget convention: minus means "already
                // open, click to close" -- when expanded; ⊞, "click to expand,"
                // when collapsed, buffer.FoldMarkerAt has an entry for it, cell
                // rendered inverted matching ScrollBar's own solid-thumb
                // convention so it visually pops). Every other row a block's
                // own EXPANDED span covers gets a guide line ('│', or '└' --
                // reusing ProjectSidebar's own box-drawing connector glyph
                // rather than inventing new Unicode -- on the span's own last
                // row) at that block's column, tracing where it closes; a
                // COLLAPSED block gets no line at all, only its header ⊞ --
                // there's nothing to trace while its body is hidden (an
                // explicit user choice, not an oversight).
                //
                // foldGutterHeaderAtColumn_/foldColumnOpenEnds_/foldColumnCursor_
                // (declared just above the row loop) turn this into a single
                // linear streaming pass over gutters_.FoldEntries()/
                // gutters_.FoldLineRangesByColumn() across the WHOLE row loop --
                // amortized O(blocks in viewport), not a fresh per-row scan or
                // binary search -- correct because same-column ranges from a
                // real syntax tree are always either disjoint or properly
                // nested (a laminar family), so a plain per-column stack,
                // advanced as `line` monotonically increases row by row, is
                // exactly the right structure: an ancestor's own range can
                // never close before a still-open descendant mapped to the
                // same (capped) column does.
                // gutter-symbol-kind follow-up: one glyph on each definition
                // line, colored via the matching SyntaxClass (a function
                // definition's glyph is colored the same as a function name
                // would be in the buffer text itself) -- same sorted-by-line
                // lower_bound lookup the blame gutter below already uses.
                // Placed right before fold, matching [digits][gap][symbol]
                // [fold][blame]'s own layout comment above.
                if (gutter.symbolWidth > 0 && static_cast<int>(gutter.symbolStart) < c.size().width) {
                    const auto it = std::lower_bound(gutters_.SymbolLineKinds().begin(), gutters_.SymbolLineKinds().end(), line,
                                                     [](const auto& entry, std::size_t l) { return entry.first < l; });
                    if (it != gutters_.SymbolLineKinds().end() && it->first == line) {
                        Cell& cell     = c[{.x = static_cast<int>(gutter.symbolStart), .y = row}];
                        cell.character = SymbolGlyphFor(it->second);
                        theme_.BrushFor(editor::SyntaxClassFor(it->second)).ApplyTo(cell);
                    }
                }

                // test-runner integration: the pass/fail mark on a discovered
                // test's own first line -- symbol block's exact lookup shape.
                if (gutter.testWidth > 0 && static_cast<int>(gutter.testStart) < c.size().width) {
                    const auto it = std::lower_bound(gutters_.TestEntries().begin(), gutters_.TestEntries().end(), line,
                                                     [](const TestGutterEntry& entry, std::size_t l) { return entry.line < l; });
                    if (it != gutters_.TestEntries().end() && it->line == line) {
                        Cell& cell            = c[{.x = static_cast<int>(gutter.testStart), .y = row}];
                        cell.character        = TestGlyphFor(it->status);
                        cell.foreground_color = TestStatusColor(it->status);
                        cell.bold             = true;
                    }
                }

                // code-coverage-gutter follow-up: the covered/uncovered/
                // partial-branch mark, test block's own lookup shape. A
                // solid color swatch (character " ", status-gutter unsaved-
                // change-indicator's own precedent) rather than a glyph --
                // this is a per-line coverage bar, not a discrete landmark
                // like the test/symbol columns either side of it. Cross-
                // referenced against diffLineKinds_ (already loaded for the
                // diff column above, no extra cache needed): an uncovered
                // line that's also newly added/modified gets a louder "!"
                // mark instead of the plain bar -- untested new code, not
                // just untested code in general. DiffLineKind::Removed is
                // excluded -- it's a deletion-boundary marker, not a real
                // line in this version of the file.
                if (gutter.coverageWidth > 0 && static_cast<int>(gutter.coverageStart) < c.size().width) {
                    const auto it = std::lower_bound(gutters_.CoverageLineStatuses().begin(), gutters_.CoverageLineStatuses().end(),
                                                     line, [](const auto& entry, std::size_t l) { return entry.first < l; });
                    if (it != gutters_.CoverageLineStatuses().end() && it->first == line) {
                        const auto diffIt  = std::lower_bound(diffLineKinds_.begin(), diffLineKinds_.end(), line,
                                                              [](const auto& entry, std::size_t l) { return entry.first < l; });
                        const bool changed = diffIt != diffLineKinds_.end() && diffIt->first == line &&
                                             diffIt->second != DiffLineKind::Removed;

                        Cell& cell = c[{.x = static_cast<int>(gutter.coverageStart), .y = row}];
                        if (it->second == editor::coverage::LineStatus::Uncovered && changed) {
                            cell.character        = "!";
                            cell.foreground_color = Color::BrightRed;
                            cell.bold             = true;
                        }
                        else {
                            Color color = Color::Green;
                            switch (it->second) {
                                case editor::coverage::LineStatus::Covered:
                                    color = Color::Green;
                                    break;
                                case editor::coverage::LineStatus::Partial:
                                    color = Color::BrightYellow;
                                    break;
                                case editor::coverage::LineStatus::Uncovered:
                                    color = Color::BrightRed;
                                    break;
                            }
                            cell.character        = " ";
                            cell.background_color = color;
                            cell.foreground_color = color;
                        }
                    }
                }

                if (gutter.foldWidth > 0) {
                    foldGutterHeaderAtColumn.fill(nullptr);
                    // <= line, not == line: when viewport_.TopLine() > 0 (scrolled past
                    // any blocks whose header sits earlier in the file), those
                    // earlier entries must still be consumed here to advance
                    // the cursor past them -- an exact-match-only condition
                    // left the cursor permanently stuck on the first entry
                    // whose headerLine falls before viewport_.TopLine(), silently
                    // suppressing every ⊞/⊟ glyph for the rest of the buffer
                    // (a real, reported bug: scrolling past ~line 50 in a file
                    // with earlier foldable blocks stopped drawing them at
                    // all). Only an exact match actually gets recorded for
                    // rendering; anything strictly earlier is skipped, not
                    // rendered, matching mid-scroll-start behavior
                    // foldColumnCursor's own analogous `<=` loop below already
                    // got right the first time.
                    while (foldGutterEntryCursor < gutters_.FoldEntries().size() &&
                           gutters_.FoldEntries()[foldGutterEntryCursor].headerLine <= line) {
                        const auto& entry = gutters_.FoldEntries()[foldGutterEntryCursor];
                        if (entry.headerLine == line) {
                            foldGutterHeaderAtColumn[entry.column] = &entry;
                        }
                        ++foldGutterEntryCursor;
                    }

                    for (int col = 0; col < kMaxFoldDepthColumns; ++col) {
                        auto&       cursor   = foldColumnCursor[col];
                        auto&       openEnds = foldColumnOpenEnds[col];
                        const auto& ranges   = gutters_.FoldLineRangesByColumn()[col];
                        while (cursor < ranges.size() && ranges[cursor].first <= line) {
                            openEnds.push_back(ranges[cursor].second);
                            ++cursor;
                        }
                        while (!openEnds.empty() && openEnds.back() <= line) {
                            openEnds.pop_back();
                        }

                        const int screenCol = static_cast<int>(gutter.foldStart) + col;
                        if (screenCol >= c.size().width) {
                            continue;
                        }
                        char32_t glyph    = U' ';
                        bool     inverted = false;
                        if (const FoldGutterEntry* header = foldGutterHeaderAtColumn[col]; header != nullptr) {
                            inverted = buffer.FoldMarkerAt(header->blockStart).has_value();
                            glyph    = inverted ? U'⊞' : U'⊟'; // ⊞ collapsed / ⊟ expanded
                        }
                        else if (!openEnds.empty()) {
                            glyph = (openEnds.back() - 1 == line) ? U'└' : U'│'; // closing row / mid-span
                        }
                        Cell& cell     = c[{.x = screenCol, .y = row}];
                        cell.character = text::EncodeCodepointUtf8(glyph);
                        gutterBrush.ApplyTo(cell);
                        cell.inverted = inverted;
                    }
                }

                // VCS blame gutter: an 8-hex-char short commit hash per
                // blamed line, color-interpolated by commit age (newer =
                // brighter) -- a directly computed Color, not routed
                // through Theme::BrushFor(SyntaxClass), same bypass the
                // diagnostic gutter's glyph coloring already uses (see
                // SpanAtOffset's own precedent) since SyntaxClass is
                // tree-sitter-capture-oriented, not a fit for this.
                if (gutter.blameWidth > 0) {
                    const auto it = std::lower_bound(blameLineInfo_.begin(), blameLineInfo_.end(), line,
                                                     [](const auto& entry, std::size_t l) { return entry.first < l; });
                    if (it != blameLineInfo_.end() && it->first == line) {
                        const std::string shortHash = it->second.commitHash.substr(0, std::min<std::size_t>(8, it->second.commitHash.size()));
                        const Color       hashColor = BlameHashColor(it->second.date);
                        for (std::size_t i = 0; i < shortHash.size() && static_cast<int>(gutter.blameStart + i) < c.size().width; ++i) {
                            Cell& cell            = c[{.x = static_cast<int>(gutter.blameStart + i), .y = row}];
                            cell.character        = std::string(1, shortHash[i]);
                            cell.foreground_color = hashColor;
                            cell.background_color = theme_.background;
                        }
                    }
                }
            } // if (segmentIndex == 0) -- line-level gutter rendering

            const std::vector<editor::HighlightSpan>& lineSpans = currentLineSpans;
            const std::vector<RenderedLink>&          lineLinks = currentLineLinks;

            std::size_t offset = currentSegment.startByte;
            // line-wrap follow-up: horizontal-scroll-follow's own
            // fast-forward phase -- consumes (but never draws) whatever
            // falls before viewport_.LeftColumn(), the same width accounting the real
            // drawing loop below uses, so the two can never disagree about
            // where a given column actually lands. viewport_.LeftColumn() stays 0 for
            // any buffer whose viewport_.EffectiveWrapLines() is true (see
            // ScrollToShowPointHorizontally's own doc comment), so this is
            // a no-op loop in that case without needing a separate check
            // here.
            if (viewport_.LeftColumn() > 0) {
                int skipped = 0;
                while (offset < currentSegment.endByte && skipped < static_cast<int>(viewport_.LeftColumn())) {
                    if (const RenderedLink* link = LinkStartingAt(lineLinks, offset)) {
                        skipped += DisplayColumns(link->displayText);
                        offset = link->endByte;
                        continue;
                    }
                    const auto decoded = content.CodepointAt(offset);
                    skipped += CodepointColumns(decoded.codepoint);
                    offset += decoded.byteLength;
                }
            }
            int col = static_cast<int>(gutter.totalWidth);
            while (offset < currentSegment.endByte && col < c.size().width) {
                if (const RenderedLink* link = LinkStartingAt(lineLinks, offset)) {
                    // Links follow-up: real Org's own "descriptive links" --
                    // the raw "[[target][description]]" markup collapses down
                    // to just its own displayText on screen, whole-hog (never
                    // truncated mid-glyph the way an ordinary too-wide line
                    // gets clipped at the viewport edge -- Org links are
                    // short enough in practice that this isn't worth the
                    // extra bookkeeping a partial-clip would need). Tab/
                    // control-byte glyphs within displayText (a realistic
                    // edge case, not assumed impossible) still go through the
                    // same expand-or-hex-placeholder treatment as ordinary
                    // buffer text -- CodepointColumns/DisplayColumns already
                    // account for their wider column cost, so the actual
                    // glyphs written here have to match or the two would
                    // silently disagree about layout.
                    const Brush      linkBrush{.background = theme_.background, .foreground = theme_.linkForeground, .bold = true};
                    std::size_t      textOffset = 0;
                    const text::Rope displayRope(link->displayText);
                    while (textOffset < displayRope.ByteLength() && col < c.size().width) {
                        const auto glyph = displayRope.CodepointAt(textOffset);
                        if (glyph.codepoint == U'\t') {
                            const int tabWidth = editor::TabWidth();
                            for (int i = 0; i < tabWidth && col < c.size().width; ++i) {
                                Cell& cell     = c[{.x = col, .y = row}];
                                cell.character = " ";
                                linkBrush.ApplyTo(cell);
                                ++col;
                            }
                        }
                        else if (IsUnprintableControl(glyph.codepoint)) {
                            const char32_t glyphs[4] = {kBinaryOpen, HexDigit((glyph.codepoint >> 4) & 0xF),
                                                        HexDigit(glyph.codepoint & 0xF), kBinaryClose};
                            for (const char32_t hexGlyph : glyphs) {
                                if (col >= c.size().width)
                                    break;
                                Cell& cell     = c[{.x = col, .y = row}];
                                cell.character = text::EncodeCodepointUtf8(hexGlyph);
                                linkBrush.ApplyTo(cell);
                                ++col;
                            }
                        }
                        else {
                            Cell& cell     = c[{.x = col, .y = row}];
                            cell.character = text::EncodeCodepointUtf8(glyph.codepoint);
                            linkBrush.ApplyTo(cell);
                            ++col;
                        }
                        textOffset += glyph.byteLength;
                    }
                    offset = link->endByte;
                    continue;
                }

                // inlayHint follow-up: unlike the link branch above, this
                // does NOT `continue` -- a hint is virtual text alongside
                // the real byte still at offset, not a replacement for it,
                // so rendering falls through to the ordinary per-character
                // path right after. Deliberately not routed through
                // SpanAtOffset/ResolvedBrush -- a fixed dimmed/italic brush
                // (theme_.ghostTextForeground, named for this: synthetic
                // virtual text, not a real SyntaxClass). Never emits a
                // raw control byte, matching every other glyph-writing loop
                // in this function.
                if (const RenderedInlayHint* hint = InlayHintStartingAt(currentLineInlayHints, offset)) {
                    const Brush      hintBrush{.background = theme_.background, .foreground = theme_.ghostTextForeground, .italic = true};
                    std::size_t      hintTextOffset = 0;
                    const text::Rope hintRope(hint->label);
                    while (hintTextOffset < hintRope.ByteLength() && col < c.size().width) {
                        const auto glyph = hintRope.CodepointAt(hintTextOffset);
                        if (glyph.codepoint >= 0x20 && glyph.codepoint != 0x7F) {
                            Cell& cell     = c[{.x = col, .y = row}];
                            cell.character = text::EncodeCodepointUtf8(glyph.codepoint);
                            hintBrush.ApplyTo(cell);
                            ++col;
                        }
                        hintTextOffset += glyph.byteLength;
                    }
                }

                const auto decoded = content.CodepointAt(offset);

                // Multi-cursor phase: a secondary caret renders as an
                // inverted cell (ScrollBar's own thumb technique) -- theme-
                // independent, and visually distinct from the primary's real
                // terminal cursor. Only the first cell of a multi-column
                // glyph (tab expansion, control placeholder) inverts.
                const bool secondaryCaretHere = IsSecondaryCursorAt(offset);

                const editor::HighlightSpan span  = SpanAtOffset(lineSpans, offset);
                Brush                       brush = ResolvedBrush(span.syntaxClass, span.captureId);
                // diagnostics-UX follow-up: underline exactly the span the
                // server flagged -- a non-disruptive "the problem is HERE"
                // cue on top of whatever syntax color the cell already has
                // (foreground deliberately untouched: recoloring would fight
                // the highlighting the way the first diff-tint attempt did).
                for (const auto& [spanStart, spanEnd] : currentLineDiagnosticSpans) {
                    if (offset >= spanStart && offset < spanEnd) {
                        brush.underlined = true;
                        break;
                    }
                }
                if (InIsearchMatch(offset)) {
                    brush.background = theme_.isearchMatchBackground;
                }
                else if (InActiveSnippetField(offset)) {
                    brush.background = theme_.snippetFieldBackground;
                }
                else if (InSelection(offset)) {
                    brush.background = theme_.selectionBackground;
                }
                else if (InConflictOurs(offset)) {
                    // Merge Conflict Resolution Mode: a persistent,
                    // must-not-miss "this is unresolved" state -- loses only
                    // to isearch/snippet-field/selection above (explicit
                    // user actions), but wins over documentHighlight/
                    // execution-line/multibuffer/trailing-whitespace below.
                    brush.background = theme_.conflictOursBackground;
                }
                else if (InConflictTheirs(offset)) {
                    brush.background = theme_.conflictTheirsBackground;
                }
                else if (InConflictBase(offset)) {
                    brush.background = theme_.conflictBaseBackground;
                }
                else if (std::any_of(currentLineDocumentHighlightSpans.begin(), currentLineDocumentHighlightSpans.end(),
                                     [offset](const auto& span) { return offset >= span.first && offset < span.second; })) {
                    // documentHighlight follow-up: a read-only cue on the
                    // symbol under point's other occurrences -- loses to
                    // isearch/snippet-field/selection above (all explicit
                    // user actions), but wins over the execution-line/
                    // multibuffer/trailing-whitespace washes below (this is
                    // still a direct answer to "what does point currently
                    // mean", a stronger signal than those cosmetic washes).
                    brush.background = theme_.documentHighlightBackground;
                }
                else if (InLineInspectHighlight(offset)) {
                    // Debugging wishlist (line-inspect follow-up): same
                    // read-only-cue priority tier as documentHighlight above
                    // -- the direct, explicit result of a dap-line-inspect
                    // the user just ran, so it still wins over the more
                    // ambient execution-line/multibuffer/trailing-whitespace
                    // washes below.
                    brush.background = theme_.lineInspectBackground;
                }
                else if (currentLineIsExecutionLine) {
                    // DAP client slice 2: the stopped line's own wash --
                    // loses to isearch/selection above (both are explicit
                    // user actions). A changed line's content area gets no
                    // tint of its own anymore (the two-column gradient that
                    // used to live here was removed per user feedback) --
                    // the diff gutter glyph plus the accent-colored line
                    // number carry the whole signal; currentLineDiffTint
                    // survives only for the latter.
                    brush.background = theme_.executionLineBackground;
                }
                else if (currentMultibufferTint) {
                    // Multibuffers follow-up: unlike the live diff gutter's
                    // content area (deliberately left untinted after user
                    // feedback that a whole-line wash fights syntax-
                    // highlighted text -- see the comment two cases above),
                    // this buffer's excerpt lines carry no syntax
                    // highlighting to fight in the first place, so a real
                    // background wash is safe and is the whole point of
                    // this dedicated diff view. Header/Rule get no
                    // background at all -- bold text and box-drawing
                    // glyphs are already visually distinct on their own
                    // (an "ASCII outline" follow-up ask), a wash would just
                    // fight the rule glyph's own default color.
                    switch (*currentMultibufferTint) {
                        case editor::multibuffer::LineTint::Added:
                            brush.background = theme_.diffAddedBackground;
                            break;
                        case editor::multibuffer::LineTint::Removed:
                            brush.background = theme_.diffRemovedBackground;
                            break;
                        case editor::multibuffer::LineTint::Header:
                            brush.bold = true;
                            break;
                        case editor::multibuffer::LineTint::Rule:
                        case editor::multibuffer::LineTint::None:
                            break;
                    }
                }
                else if (editor::TrailingWhitespaceHighlightEnabled() && offset >= currentLineTrailingWhitespaceStart) {
                    // Whitespace-visualization follow-up: lowest priority of
                    // this chain, same reasoning as every case above it --
                    // an active isearch/selection/execution/multibuffer
                    // overlay always wins over this purely cosmetic wash.
                    // offset >= currentLineTrailingWhitespaceStart already
                    // guarantees this cell is a space/tab (see that field's
                    // own doc comment), so no codepoint check is needed here.
                    brush.background = theme_.trailingWhitespaceBackground;
                }

                if (decoded.codepoint == U'\t') {
                    // A real terminal treats a raw tab byte as "jump to the next
                    // tab stop" (consuming several columns), not "print one
                    // glyph and advance by one" -- sending it through unexpanded
                    // desyncs the terminal's actual cursor position from what
                    // the terminal library's own per-cell diff bookkeeping
                    // believes was written, which then corrupts unrelated cells
                    // on later frames. Expanding to literal space glyphs keeps
                    // this widget's one-codepoint-per-column model -- and the
                    // real terminal's actual column count -- in agreement.
                    // editor::TabWidth() is a *display* setting only; the
                    // buffer's real tab byte is untouched.
                    const int  tabWidth = editor::TabWidth();
                    const bool inIndent = editor::IndentGuidesEnabled() && offset < currentLineIndentEnd;
                    for (int i = 0; i < tabWidth && col < c.size().width; ++i) {
                        Cell&     cell          = c[{.x = col, .y = row}];
                        const int displayColumn = col - static_cast<int>(gutter.totalWidth);
                        if (inIndent && displayColumn > 0 && displayColumn % tabWidth == 0) {
                            // Whitespace-visualization follow-up: a guide
                            // glyph in place of one of the expanded tab's
                            // space cells, at each indent-width column --
                            // see currentLineIndentEnd's own doc comment.
                            cell.character        = text::EncodeCodepointUtf8(kIndentGuide);
                            Brush guideBrush      = brush;
                            guideBrush.foreground = IndentGuideColor(theme_, displayColumn, tabWidth);
                            guideBrush.ApplyTo(cell);
                        }
                        else {
                            cell.character = " ";
                            brush.ApplyTo(cell);
                        }
                        if (i == 0 && secondaryCaretHere) {
                            cell.inverted = true;
                        }
                        ++col;
                    }
                }
                else if (IsUnprintableControl(decoded.codepoint)) {
                    // Same reasoning as the tab case above: a raw control byte
                    // (some of them genuine terminal control codes -- a bare ESC
                    // is the sharpest example) must never reach the terminal as
                    // itself. Rendered as a 4-column "◁XX▷" hex placeholder
                    // instead -- entirely safe, printable characters -- with a
                    // dedicated foreground so it reads as "this is escaped data",
                    // not literal text; whatever background isearch/selection
                    // already chose above is kept so an active highlight still
                    // shows through it.
                    Brush binaryBrush             = brush;
                    binaryBrush.foreground        = theme_.binaryForeground;
                    const char32_t glyphs[4]      = {kBinaryOpen, HexDigit((decoded.codepoint >> 4) & 0xF),
                                                     HexDigit(decoded.codepoint & 0xF), kBinaryClose};
                    bool           firstGlyphCell = true;
                    for (const char32_t glyph : glyphs) {
                        if (col >= c.size().width) {
                            break;
                        }
                        Cell& cell     = c[{.x = col, .y = row}];
                        cell.character = text::EncodeCodepointUtf8(glyph);
                        binaryBrush.ApplyTo(cell);
                        if (firstGlyphCell && secondaryCaretHere) {
                            cell.inverted  = true;
                            firstGlyphCell = false;
                        }
                        ++col;
                    }
                }
                else {
                    Cell&     cell          = c[{.x = col, .y = row}];
                    const int displayColumn = col - static_cast<int>(gutter.totalWidth);
                    // offset < currentLineIndentEnd here only ever holds for
                    // a space cell (see that field's own doc comment: the
                    // scan that computes it stops at the first non-space/tab
                    // byte), so decoded.codepoint is guaranteed U' ' whenever
                    // this substitutes a guide glyph.
                    if (editor::IndentGuidesEnabled() && offset < currentLineIndentEnd && displayColumn > 0 &&
                        displayColumn % editor::TabWidth() == 0) {
                        cell.character        = text::EncodeCodepointUtf8(kIndentGuide);
                        Brush guideBrush      = brush;
                        guideBrush.foreground = IndentGuideColor(theme_, displayColumn, editor::TabWidth());
                        guideBrush.ApplyTo(cell);
                    }
                    else {
                        cell.character = text::EncodeCodepointUtf8(decoded.codepoint);
                        brush.ApplyTo(cell);
                    }
                    if (secondaryCaretHere) {
                        cell.inverted = true;
                    }
                    ++col;
                }

                offset += decoded.byteLength;
            }

            // wrap-continuation-indicator follow-up: every row that hands
            // off to another wrap segment of the same line gets this glyph
            // pinned to the true right edge (the column ComputeWrapSegments'
            // own caller reserved above) rather than trailing wherever the
            // segment's own last word happened to end -- a smart/word-aware
            // wrap segment routinely ends well short of the edge, and the
            // point is a clearly-positioned "this continues" cue, not a
            // caret glued to the last rendered word.
            if (segmentIndex + 1 < lineSegments.size()) {
                const Brush wrapContinuationBrush{.background = theme_.background, .foreground = theme_.lineNumberForeground};
                Cell&       cell = c[{.x = c.size().width - 1, .y = row}];
                cell.character   = text::EncodeCodepointUtf8(kWrapContinuationIndicator);
                wrapContinuationBrush.ApplyTo(cell);
            }

            // line-truncation-indicator follow-up: offset < endByte here
            // means the content loop above stopped because it ran out of
            // viewport width, not because it reached the end of what this
            // row actually has to show -- only reachable with wrap off (a
            // wrapped segment never exceeds the viewport width by
            // construction, see ComputeWrapSegments's own doc comment).
            // Overwrites the row's own last-drawn column rather than
            // reserving a dedicated one, the same "small, unobtrusive
            // marker" approach the fold-ellipsis glyph just below already
            // takes for a conceptually similar "there's more here" cue.
            // Multi-cursor phase: a secondary caret sitting exactly at this
            // line's content end (the newline position -- end-of-line motion
            // parks cursors there constantly) has no codepoint cell of its
            // own above; invert the first padding cell instead. Last
            // segment only: a mid-wrap segment's endByte is the next
            // segment's startByte, which the content loop already covers.
            if (segmentIndex + 1 == lineSegments.size() && col < c.size().width &&
                IsSecondaryCursorAt(currentSegment.endByte)) {
                c[{.x = col, .y = row}].inverted = true;
            }

            if (offset < currentSegment.endByte && col > 0) {
                const Brush truncationBrush{.background = theme_.background, .foreground = theme_.truncationIndicatorForeground};
                Cell&       cell = c[{.x = col - 1, .y = row}];
                cell.character   = text::EncodeCodepointUtf8(kTruncationIndicator);
                truncationBrush.ApplyTo(cell);
            }

            // line-wrap follow-up: this ellipsis represents "content AFTER
            // this line is hidden" -- belongs on the line's own last visual
            // row, not every wrap continuation row.
            if (segmentIndex + 1 == lineSegments.size()) {
                // Org-mode fold/unfold follow-up: any marked headline (Collapsed or
                // ChildrenVisible -- either way, something below this line is
                // currently hidden) gets a short ellipsis painted right after its
                // own content, real Org's own visual cue that there's more here
                // than what's shown. Reuses theme_.lineNumberForeground rather than
                // a new dedicated Theme field -- deliberately minimal, a distinct
                // color is an easy follow-up if it turns out to matter in practice.
                // generic-code-folding follow-up: was `buffer.FoldMarkerAt(lineStart).has_value()`
                // -- correct for Org, whose marker key always IS the headline
                // line's own start byte, but not for a code fold, whose marker
                // key is a foldable block's own startByte (e.g. a function's
                // "{"), which sits partway through its header line, not at
                // column 0. Checking viewport_.HiddenLineRanges() for an entry starting
                // right after this line is the one condition both fold sources
                // agree on (see FoldedLineRanges' own [startLine+1, endLine+1)
                // convention, shared by org:: and codefold:: alike), so this is
                // the generic trigger both the ellipsis and the preview below
                // key off, rather than a marker lookup at all.
                for (const auto& [hiddenStart, hiddenEnd] : viewport_.HiddenLineRanges()) {
                    if (hiddenStart != line + 1 || hiddenEnd == hiddenStart) {
                        continue;
                    }
                    const Brush foldBrush{.background = theme_.background, .foreground = theme_.lineNumberForeground};
                    for (const char32_t glyph : {U' ', kFoldEllipsis}) {
                        if (col >= c.size().width)
                            break;
                        Cell& cell     = c[{.x = col, .y = row}];
                        cell.character = text::EncodeCodepointUtf8(glyph);
                        foldBrush.ApplyTo(cell);
                        ++col;
                    }

                    // A short, dim preview of the folded region's own last line
                    // (e.g. a closing "}" or "};") right after the ellipsis, so
                    // collapsing a block doesn't fully erase what its closing
                    // line looked like -- every folded line is hidden from
                    // rendering by definition, so this preview is the only way
                    // that line's content ever reaches the screen while the
                    // fold is closed. Leading whitespace is skipped (this is a
                    // preview snippet, not a faithful column-accurate render,
                    // so the closing line's own indentation would just waste
                    // columns).
                    const std::size_t lastHiddenLine = hiddenEnd - 1;
                    std::size_t       previewOffset  = content.LineToByteOffset(lastHiddenLine);
                    const std::size_t previewEnd     = (lastHiddenLine + 1 < totalLines)
                                                           ? content.LineToByteOffset(lastHiddenLine + 1) - 1
                                                           : content.ByteLength();
                    while (previewOffset < previewEnd) {
                        const auto decoded = content.CodepointAt(previewOffset);
                        if (decoded.codepoint != U' ' && decoded.codepoint != U'\t') {
                            break;
                        }
                        previewOffset += decoded.byteLength;
                    }
                    if (previewOffset >= previewEnd || col >= c.size().width) {
                        break;
                    }
                    const Brush previewBrush{.background = theme_.background, .foreground = theme_.lineNumberForeground};
                    {
                        Cell& spaceCell     = c[{.x = col, .y = row}];
                        spaceCell.character = " ";
                        previewBrush.ApplyTo(spaceCell);
                        ++col;
                    }
                    while (previewOffset < previewEnd && col < c.size().width) {
                        const auto decoded = content.CodepointAt(previewOffset);
                        Cell&      cell    = c[{.x = col, .y = row}];
                        cell.character     = text::EncodeCodepointUtf8(decoded.codepoint);
                        previewBrush.ApplyTo(cell);
                        ++col;
                        previewOffset += decoded.byteLength;
                    }
                    break;
                }
            } // if (segmentIndex + 1 == lineSegments.size()) -- fold ellipsis/preview

            // trailing-blank-line-gutter follow-up: the buffer's own true
            // last line (line + 1 == totalLines) never gets a phantom empty
            // line after it when it has content -- lineEnd > lineStart here
            // is only possible when the buffer's own final byte is NOT a
            // newline (see ITextStorage::LineCount()'s "newline count + 1"
            // contract, and isEmptyTrailingPhantomLine's own doc comment
            // above). Marks that clearly rather than leaving the reader to
            // guess whether an invisible blank line follows. Reflects the
            // LIVE buffer only -- Editor/FinalNewline.h's own
            // EnsureFinalNewline appends a trailing newline on save but,
            // by design, never touches the in-memory Rope/undo tree, so
            // this can still show right after a save with that setting on;
            // a deliberate, documented gap in that subsystem, not a bug
            // here. Skipped when there's no free column left (an
            // already-viewport-filling last line with wrap off) rather than
            // clobbering real content the way the truncation indicator does
            // -- this is a much rarer case and not worth the same trade-off.
            if (line + 1 == totalLines && lineEnd > lineStart && segmentIndex + 1 == lineSegments.size() &&
                col < c.size().width) {
                const Brush noNewlineBrush{.background = theme_.background, .foreground = theme_.lineNumberForeground};
                Cell&       cell = c[{.x = col, .y = row}];
                cell.character   = text::EncodeCodepointUtf8(kNoTrailingNewlineIndicator);
                noNewlineBrush.ApplyTo(cell);
                ++col;
            }

            // completion-popup follow-up: completion no longer paints
            // anything inline here -- ActiveCompletion renders via a real
            // ListPopup overlay instead (NotifyCompletionChanged, called at
            // every activeCompletion_ mutation site plus once per Paint()
            // below when point's on-screen position has moved).

            // prose-diagnostic-callout follow-up: this row's real content
            // ends at `col` (everything painted above it -- text, truncation
            // indicator, fold ellipsis/preview -- has already advanced it)
            // -- see rowLine/rowContentEndColumn's own doc comment above the
            // row loop.
            rowLine[row]             = line;
            rowContentEndColumn[row] = col;

            // line-wrap follow-up: advance to the next wrap segment (row)
            // of the same buffer line if there is one, otherwise advance to
            // the next visible buffer line -- was an unconditional
            // `line = viewport_.NextVisibleLine(line + 1, renderEndLine)` before wrap
            // existed, which segmentIndex staying 0 (lineSegments always
            // exactly one entry) reduces to exactly.
            if (segmentIndex + 1 < lineSegments.size()) {
                ++segmentIndex;
            }
            else {
                // inline-diagnostics follow-up: the annotation row renders
                // after the line's LAST wrap row, before the next line.
                if (AnnotationRowsForLine(line) > 0) {
                    pendingAnnotationLine = line;
                }
                segmentIndex = 0;
                line         = viewport_.NextVisibleLine(line + 1, renderEndLine);
            }
        }
        else {
            // prose-diagnostic-callout follow-up: past end-of-buffer -- this
            // row was already blanked by the top-of-loop wash, genuinely
            // empty past the gutter, so a callout brace may use it as
            // padding.
            rowContentEndColumn[row] = static_cast<int>(gutter.totalWidth);
            line                     = viewport_.NextVisibleLine(line + 1, renderEndLine);
        }
    }

    PaintProseDiagnosticCallouts(c, rowLine, rowContentEndColumn, gutter.totalWidth);

    // completion-popup follow-up: activeCompletion_ mutation sites already
    // notify onCompletionChanged_ directly (NotifyCompletionChanged), but
    // none of them fire on a pure scroll -- nothing about ActiveCompletion
    // itself changes when the view scrolls, only where point now renders.
    // This is the cheap per-frame catch-up for exactly that case: recompute
    // the anchor and re-notify only when it actually moved (including
    // moving to/from "off screen", i.e. std::nullopt) -- comparing anchors
    // first (cheap: two field reads) avoids rebuilding/resending the whole
    // popup model on every ordinary repaint while nothing about it changed.
    if (activeCompletion_ && CompletionAnchorNow() != lastNotifiedCompletionAnchor_) {
        NotifyCompletionChanged();
    }
}

void BufferView::PaintInlineDiagnosticRow(Canvas& c, int row, std::size_t line, std::size_t gutterWidth) {
    const auto it = gutters_.InlineDiagnosticsByLine().find(line);
    if (it == gutters_.InlineDiagnosticsByLine().end()) {
        return; // shouldn't happen (RowsForLine and Paint share the cache within one frame) -- leave the blanked row
    }
    const InlineDiagnostic& diagnostic = it->second;

    // Annotation styling is deliberately NOT plain severity-colored text --
    // a user report caught exactly that reading as ordinary code (the
    // warning amber sits right next to similarly-warm syntax hues): the
    // message renders in italic, and the severity's own gutter glyph is
    // repeated between the carets and the message, so an annotation row is
    // recognizable as one at a glance in any theme.
    const Color color = DiagnosticSeverityColor(theme_, diagnostic.severity);
    const Brush caretBrush{.background = theme_.background, .foreground = color, .bold = true};
    const Brush messageBrush{.background = theme_.background, .foreground = color, .italic = true};

    const int width = c.size().width;
    int       col   = static_cast<int>(gutterWidth);

    // Carets under the diagnostic's visual span -- only when the column
    // positions on the row above are trustworthy: wrap off (the annotation
    // sits below the line's LAST wrap row, where first-row column math
    // would lie) and the span's start still on-screen horizontally.
    if (!viewport_.EffectiveWrapLines()) {
        const text::Buffer&       buffer    = activeBuffer_.Get();
        const text::ITextStorage& content   = buffer.Content();
        const std::size_t         lineStart = content.LineToByteOffset(line);
        const std::size_t         lineEnd =
            (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        const std::vector<RenderedLink> lineLinks = LinksForLine(viewport_.Links(), lineStart, lineEnd, buffer.Point());

        // Same viewport_.LeftColumn()-aware bound/offset arithmetic CursorPosition uses.
        const int                bound = width + static_cast<int>(viewport_.LeftColumn());
        const std::optional<int> startCol =
            VisualColumn(content, lineStart, std::min(diagnostic.startByte, lineEnd), bound, lineLinks);
        if (startCol && *startCol >= static_cast<int>(viewport_.LeftColumn())) {
            const std::optional<int> endCol =
                VisualColumn(content, lineStart, std::min(diagnostic.endByte, lineEnd), bound, lineLinks);
            const int screenStart = static_cast<int>(gutterWidth) + *startCol - static_cast<int>(viewport_.LeftColumn());
            // A span running past the visual-column bound (endCol nullopt)
            // degrades to a single caret at its start rather than flooding
            // the row -- the message is the more useful content to keep.
            const int caretCount = endCol ? std::max(1, *endCol - *startCol) : 1;
            for (int i = 0; i < caretCount && screenStart + i < width; ++i) {
                Cell& cell     = c[{.x = screenStart + i, .y = row}];
                cell.character = "^";
                caretBrush.ApplyTo(cell);
            }
            col = std::min(screenStart + caretCount, width) + 1;
        }
    }

    // The severity's gutter glyph, repeated here so the annotation carries
    // the same iconography the gutter column uses (see the styling comment
    // above) -- then the message, truncated at the viewport; byte-per-cell,
    // the same ASCII-ish rendering simplification ModeLine's own text
    // documents.
    const DiagnosticGlyph glyph = DiagnosticGlyphFor(diagnostic.severity);
    if (col < width) {
        Cell& cell     = c[{.x = col, .y = row}];
        cell.character = glyph.glyph;
        Brush{.background = theme_.background, .foreground = color, .bold = glyph.bold}.ApplyTo(cell);
        col += 2; // glyph, then one separating space
    }
    for (const char ch : diagnostic.message) {
        if (col >= width) {
            break;
        }
        Cell& cell     = c[{.x = col, .y = row}];
        cell.character = std::string(1, ch);
        messageBrush.ApplyTo(cell);
        ++col;
    }
}

void BufferView::PaintProseDiagnosticCallouts(Canvas& c, const std::vector<std::size_t>& rowLine,
                                              const std::vector<int>& rowContentEndColumn, std::size_t gutterWidth) {
    const int height = c.size().height;
    const int width  = c.size().width;

    // First/last screen row currently showing each buffer line's content --
    // see this method's own header-comment for why an entry's absence here
    // (a line scrolled/folded out of view) means "skip this diagnostic's
    // callout entirely," not "clamp it."
    std::unordered_map<std::size_t, std::pair<int, int>> lineRowRange;
    for (int row = 0; row < height; ++row) {
        if (rowLine[row] == kNoRowLine) {
            continue;
        }
        auto [it, inserted] = lineRowRange.try_emplace(rowLine[row], row, row);
        if (!inserted) {
            it->second.first  = std::min(it->second.first, row);
            it->second.second = std::max(it->second.second, row);
        }
    }
    if (lineRowRange.empty()) {
        return;
    }

    const text::Buffer&       buffer     = activeBuffer_.Get();
    const text::ITextStorage& content    = buffer.Content();
    const std::size_t         byteLength = content.ByteLength();
    const auto&               glyphs     = RoundedBorderGlyphs();

    // Gathering pass: every on-screen Prose diagnostic reduced to a
    // ProseCalloutItem, sorted by its own firstRow -- the clustering pass
    // right below needs that ordering to do a single linear merge.
    std::vector<ProseCalloutItem> items;
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        if (diagnostic.origin != text::Buffer::Diagnostic::Origin::Prose) {
            continue;
        }

        const std::size_t startByte = std::min(diagnostic.startByte, byteLength);
        const std::size_t lastByte =
            diagnostic.endByte > diagnostic.startByte ? std::min(diagnostic.endByte - 1, byteLength) : startByte;
        const std::size_t firstLine = content.ByteOffsetToLine(startByte);
        const std::size_t lastLine  = content.ByteOffsetToLine(lastByte);

        const auto firstIt = lineRowRange.find(firstLine);
        const auto lastIt  = lineRowRange.find(lastLine);
        if (firstIt == lineRowRange.end() || lastIt == lineRowRange.end()) {
            continue; // the flagged block isn't fully on screen -- gutter icon + bottom hint carry it instead
        }

        const int firstRow = firstIt->second.first;
        const int lastRow  = lastIt->second.second;
        if (firstRow > lastRow) {
            continue; // defensive -- shouldn't happen, rows only ever advance with `line`
        }

        items.push_back(ProseCalloutItem{
            .firstRow   = firstRow,
            .lastRow    = lastRow,
            .messageRow = firstRow + (lastRow - firstRow) / 2,
            .severity   = diagnostic.severity,
            .message    = diagnostic.message.substr(0, diagnostic.message.find('\n')),
        });
    }
    if (items.empty()) {
        return;
    }
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.firstRow < b.firstRow; });

    // Clustering pass: a run of items whose own [firstRow, lastRow] blocks
    // sit within one row of each other (touching once each gets its usual
    // 1-row corner padding) shares a single spine instead of each getting
    // its own separate corners -- exactly the "group them together when
    // there's a whole bunch" a page of clustered comment/prose diagnostics
    // otherwise produces. clusterStart/clusterEnd index into `items`
    // ([start, end)); clusterLastRow tracks the running max lastRow seen so
    // far in the open cluster (items are sorted by firstRow, not lastRow,
    // so a later item's own block can still end earlier than an in-progress
    // one -- max, not simply the latest item's own lastRow).
    std::size_t clusterStart   = 0;
    int         clusterLastRow = items[0].lastRow;
    for (std::size_t i = 1; i <= items.size(); ++i) {
        const bool endOfRun = i == items.size() || items[i].firstRow > clusterLastRow + 2;
        if (!endOfRun) {
            clusterLastRow = std::max(clusterLastRow, items[i].lastRow);
            continue;
        }

        const std::size_t clusterEnd      = i;
        const int         clusterFirstRow = items[clusterStart].firstRow;
        const int         topRow          = clusterFirstRow > 0 ? clusterFirstRow - 1 : clusterFirstRow;
        const int         bottomRow       = clusterLastRow + 1 < height ? clusterLastRow + 1 : clusterLastRow;

        int anchorCol = static_cast<int>(gutterWidth);
        for (int row = topRow; row <= bottomRow; ++row) {
            anchorCol = std::max(anchorCol, rowContentEndColumn[row]);
        }
        anchorCol += 2; // small gap between the pane's own text and the callout

        constexpr int kBranchOverhead  = 3; // "├─ "
        constexpr int kMinMessageChars = 6; // not worth drawing a callout that can't show a few real words
        if (anchorCol + kBranchOverhead + kMinMessageChars <= width) {
            // Most-severe item in the cluster colors its shared spine
            // (corners + vertical bar) -- same "most severe wins" precedent
            // EnsureDiagnosticGutterCache's own per-line collapse already
            // uses; each branch row keeps its OWN item's severity color,
            // same as before clustering existed.
            std::size_t mostSevere = clusterStart;
            for (std::size_t i2 = clusterStart + 1; i2 < clusterEnd; ++i2) {
                if (DiagnosticSeverityRank(items[i2].severity) > DiagnosticSeverityRank(items[mostSevere].severity)) {
                    mostSevere = i2;
                }
            }
            const Brush spineBrush{
                .background = theme_.background, .foreground = DiagnosticSeverityColor(theme_, items[mostSevere].severity), .bold = true};

            // Row -> the (possibly several, on a messageRow collision)
            // items landing on it -- built once per cluster rather than
            // scanning all of clusterStart..clusterEnd per row.
            std::unordered_map<int, std::vector<std::size_t>> itemsByMessageRow;
            for (std::size_t i2 = clusterStart; i2 < clusterEnd; ++i2) {
                itemsByMessageRow[items[i2].messageRow].push_back(i2);
            }

            for (int row = topRow; row <= bottomRow; ++row) {
                const auto rowIt = itemsByMessageRow.find(row);
                if (rowIt != itemsByMessageRow.end()) {
                    const ProseCalloutItem& first   = items[rowIt->second.front()];
                    std::string             message = first.message;
                    if (rowIt->second.size() > 1) {
                        message += " (+" + std::to_string(rowIt->second.size() - 1) + " more)";
                    }
                    const Brush branchBrush{
                        .background = theme_.background, .foreground = DiagnosticSeverityColor(theme_, first.severity), .bold = true};
                    const Brush messageBrush{
                        .background = theme_.background, .foreground = DiagnosticSeverityColor(theme_, first.severity), .italic = true};

                    int col = anchorCol;
                    for (const char32_t glyph : {U'├', glyphs.horizontal}) {
                        Cell& cell     = c[{.x = col, .y = row}];
                        cell.character = text::EncodeCodepointUtf8(glyph);
                        branchBrush.ApplyTo(cell);
                        ++col;
                    }
                    if (col < width) {
                        Cell& cell     = c[{.x = col, .y = row}];
                        cell.character = " ";
                        branchBrush.ApplyTo(cell);
                        ++col;
                    }
                    for (const char ch : message) {
                        if (col >= width) {
                            break;
                        }
                        Cell& cell     = c[{.x = col, .y = row}];
                        cell.character = std::string(1, ch);
                        messageBrush.ApplyTo(cell);
                        ++col;
                    }
                }
                else {
                    const char32_t glyph = (row == topRow)      ? glyphs.topRight
                                           : (row == bottomRow) ? glyphs.bottomRight
                                                                : glyphs.vertical;
                    Cell&          cell  = c[{.x = anchorCol, .y = row}];
                    cell.character       = text::EncodeCodepointUtf8(glyph);
                    spineBrush.ApplyTo(cell);
                }
            }
        }
        // else: no room -- drop the whole cluster, relying on the gutter
        // icon + bottom hint, matching a single dropped callout's own
        // "otherwise the hint is enough" fallback.

        if (i < items.size()) {
            clusterStart   = i;
            clusterLastRow = items[i].lastRow;
        }
    }
}

std::optional<Point> BufferView::CursorPosition() const {
    // A pure, independently-callable query, deliberately NOT a value cached
    // as a Paint() side effect -- main.cpp's render() calls this once,
    // separately, after the whole widget tree's Paint() pass has already
    // completed for the frame. Cheap enough to recompute on every call --
    // buffer/content access, one GutterWidth() call, one
    // ByteOffsetToLine/LineToByteOffset pair, one bounded VisualColumn scan
    // -- nowhere near Paint()'s own per-visible-row cost.
    const text::Buffer&       buffer      = activeBuffer_.Get();
    const text::ITextStorage& content     = buffer.Content();
    const std::size_t         point       = buffer.Point();
    const std::size_t         pointLine   = content.ByteOffsetToLine(point);
    const std::size_t         gutterWidth = GutterWidth();

    // Org-mode fold/unfold follow-up: a point sitting on a currently-hidden
    // line has no on-screen row to report at all -- can't happen through
    // org-cycle itself (see CycleFoldAtPoint's own doc comment), but stays
    // a real, harmless "no cursor this frame" rather than an invalid
    // position if some other path ever moves point into a hidden region.
    if (pointLine < viewport_.TopLine() || viewport_.IsLineHidden(pointLine)) {
        return std::nullopt;
    }

    // size() is still its default-constructed {0,0} if this is called
    // before SetBox_ has ever run on this widget (e.g. a headless caller
    // querying CursorPosition() before any Paint() pass) -- treating an
    // unknown ({0,0}) size as "don't bound at all" rather than "assume
    // everything is off-screen" is what keeps the cursor visible in that
    // case instead of appearing to not exist.
    const Size sizeNow     = size();
    const bool sizeIsKnown = sizeNow.height > 0 && sizeNow.width > 0;

    const std::size_t lineStart = content.LineToByteOffset(pointLine);
    const std::size_t lineEnd =
        (pointLine + 1 < content.LineCount()) ? content.LineToByteOffset(pointLine + 1) - 1 : content.ByteLength();

    // Links follow-up: point's own line never has a link collapsed AT
    // point's own position (LinksForLine excludes any link containing
    // point), so this always agrees with what Paint() actually drew for
    // this specific row.
    const std::vector<RenderedLink> lineLinks = LinksForLine(viewport_.Links(), lineStart, lineEnd, point);

    // line-wrap follow-up: which wrap segment (row) of pointLine actually
    // contains point -- 0, and the whole line as one segment, when wrap is
    // off or the viewport size isn't known yet (mirrors the rest of this
    // method's own "unknown size means don't try to bound" tolerance).
    // Prefers the earliest segment point is strictly inside; only the
    // line's own LAST segment also accepts point sitting exactly at its
    // end (point at end-of-line) -- a point sitting exactly at an earlier
    // segment's own boundary belongs to the NEXT segment instead (the
    // start of a new visual row), matching how a real editor's cursor
    // behaves at a wrapped line break.
    std::size_t rowWithinLine = 0;
    std::size_t segmentStart  = lineStart;
    if (viewport_.EffectiveWrapLines() && sizeIsKnown) {
        const int                      fullWidth = std::max(1, sizeNow.width - static_cast<int>(gutterWidth));
        const std::vector<WrapSegment> segments  = ComputeWrappedLineSegments(content, lineStart, lineEnd, fullWidth, lineLinks);
        for (std::size_t i = 0; i < segments.size(); ++i) {
            const bool isLast = (i + 1 == segments.size());
            if (point >= segments[i].startByte && (point < segments[i].endByte || (isLast && point == segments[i].endByte))) {
                rowWithinLine = i;
                segmentStart  = segments[i].startByte;
                break;
            }
        }
    }

    // main-editor-sticky-scroll follow-up: visibleRow is still relative to
    // viewport_.TopLine()'s own screen row (row 0) -- stickyRowCount_ is added to the
    // final Point below, once, rather than threaded through every
    // intermediate row computation above; the bound check here has to widen
    // by the same amount first, or a point that's genuinely still on screen
    // (just pushed down by the pinned rows) would be wrongly reported as
    // off-screen.
    const std::size_t visibleRow = viewport_.VisibleRowCountBetween(viewport_.TopLine(), pointLine) + rowWithinLine;
    if (sizeIsKnown && visibleRow + static_cast<std::size_t>(stickyRowCount_) >= static_cast<std::size_t>(sizeNow.height)) {
        return std::nullopt;
    }

    // line-wrap follow-up: horizontal-scroll-follow -- the scan has to walk
    // far enough right to still find point even when scrolled, so the bound
    // grows by viewport_.LeftColumn() (only meaningful once sizeIsKnown -- an unknown
    // size already means "don't bound at all"); the true on-screen column
    // is the raw column minus viewport_.LeftColumn(), subtracted back out below.
    // viewport_.LeftColumn() is always 0 once viewport_.EffectiveWrapLines() is true (see
    // ScrollToShowPointHorizontally), so this is a no-op adjustment then.
    const int maxColumns = sizeIsKnown ? sizeNow.width - static_cast<int>(gutterWidth) + static_cast<int>(viewport_.LeftColumn())
                                       : std::numeric_limits<int>::max();

    const std::optional<int> visualCol = VisualColumn(content, segmentStart, point, maxColumns, lineLinks);
    if (!visualCol || *visualCol < static_cast<int>(viewport_.LeftColumn())) {
        return std::nullopt; // scrolled off the left edge -- shouldn't happen once viewport_.LeftColumn() is correct, but a safe guard
    }

    const std::size_t col = gutterWidth + static_cast<std::size_t>(*visualCol) - viewport_.LeftColumn();
    if (sizeIsKnown && col >= static_cast<std::size_t>(sizeNow.width)) {
        return std::nullopt; // scrolled off horizontally to the right
    }
    return Point{.x = static_cast<int>(col), .y = static_cast<int>(visibleRow) + stickyRowCount_};
}

bool BufferView::InSelection(std::size_t byteOffset) const {
    const text::Buffer& buffer = activeBuffer_.Get();
    if (buffer.HasMark()) {
        const auto [start, end] = buffer.Region();
        if (byteOffset >= start && byteOffset < end) {
            return true;
        }
    }
    // Multi-cursor phase: each secondary cursor's own selection highlights
    // through the exact same overlay -- per-codepoint x per-secondary is
    // fine at real cursor counts (a handful), the same reasoning
    // currentLineDiagnosticSpans' own per-cell loop already relies on.
    for (const auto& cursor : buffer.SecondaryCursors()) {
        if (!cursor.mark) {
            continue;
        }
        const std::size_t start = std::min(cursor.point, *cursor.mark);
        const std::size_t end   = std::max(cursor.point, *cursor.mark);
        if (byteOffset >= start && byteOffset < end) {
            return true;
        }
    }
    return false;
}

bool BufferView::IsSecondaryCursorAt(std::size_t byteOffset) const {
    for (const auto& cursor : activeBuffer_.Get().SecondaryCursors()) {
        if (cursor.point == byteOffset) {
            return true;
        }
    }
    return false;
}

Brush BufferView::ResolvedBrush(editor::SyntaxClass cls, editor::CaptureId captureId) const {
    // Flush on any style change (ned/set-syntax-* and ned/set-capture-*
    // share one generation, SyntaxTheme.h) -- one locked counter read per
    // call, versus the several locked map lookups plus a name lookup the
    // capture-aware BrushFor does on a miss.
    const std::size_t generation = editor::SyntaxThemeGeneration();
    if (generation != brushCacheGeneration_ || theme_.name != brushCacheThemeName_) {
        // The name check covers select-theme: the applier (main.cpp)
        // assigns a whole new Theme into the one object theme_ refers to,
        // which moves no SyntaxTheme generation -- every real theme (and
        // both preview directions) carries a distinct name, and nothing
        // mutates a live Theme's fields under an unchanged name today.
        brushCache_.clear();
        brushCacheGeneration_ = generation;
        brushCacheThemeName_  = theme_.name;
    }

    const std::uint32_t key = (static_cast<std::uint32_t>(cls) << 16) | captureId;
    if (const auto it = brushCache_.find(key); it != brushCache_.end()) {
        return it->second;
    }
    const Brush brush = theme_.BrushFor(cls, captureId);
    brushCache_.emplace(key, brush);
    return brush;
}

bool BufferView::InActiveSnippetField(std::size_t byteOffset) const {
    if (inputMode_ != InputMode::Snippet || !snippetSession_) {
        return false;
    }
    // Reads the *active* buffer's ranges -- if it isn't the session buffer
    // (a transient mid-frame mismatch; the session ends on a real switch)
    // its range set is empty and this simply reports false.
    const auto range = snippetSession_->ActiveFieldRange(activeBuffer_.Get());
    return range && byteOffset >= range->first && byteOffset < range->second;
}

bool BufferView::InLineInspectHighlight(std::size_t byteOffset) const {
    if (!lineInspect_ || lineInspect_->buffer != &activeBuffer_.Get() ||
        lineInspect_->contentGeneration != activeBuffer_.Get().ContentGeneration()) {
        return false; // stale: buffer switched or content changed since dap-line-inspect ran
    }
    for (const auto& [start, end] : lineInspect_->ranges) {
        if (byteOffset >= start && byteOffset < end) {
            return true;
        }
    }
    return false;
}

bool BufferView::InConflictOurs(std::size_t byteOffset) const {
    const std::vector<text::ConflictHunk>& hunks = gutters_.ConflictHunks();
    return std::any_of(hunks.begin(), hunks.end(),
                       [byteOffset](const text::ConflictHunk& hunk) { return InRange(byteOffset, hunk.oursRange); });
}

bool BufferView::InConflictTheirs(std::size_t byteOffset) const {
    const std::vector<text::ConflictHunk>& hunks = gutters_.ConflictHunks();
    return std::any_of(hunks.begin(), hunks.end(),
                       [byteOffset](const text::ConflictHunk& hunk) { return InRange(byteOffset, hunk.theirsRange); });
}

bool BufferView::InConflictBase(std::size_t byteOffset) const {
    const std::vector<text::ConflictHunk>& hunks = gutters_.ConflictHunks();
    return std::any_of(hunks.begin(), hunks.end(), [byteOffset](const text::ConflictHunk& hunk) {
        return hunk.baseRange && InRange(byteOffset, *hunk.baseRange);
    });
}

bool BufferView::InIsearchMatch(std::size_t byteOffset) const {
    if (!search_ || !search_->Found()) {
        return false;
    }

    const std::string& query = search_->Query();
    if (query.empty()) {
        return false;
    }

    // Forward search leaves point at the match end, backward at the match
    // start (IncrementalSearch's own documented convention) -- recover the
    // full range from point + query length rather than IncrementalSearch
    // exposing it directly, since nothing else needs that.
    std::size_t       matchStart;
    std::size_t       matchEnd;
    const std::size_t point = activeBuffer_.Get().Point();
    if (inputMode_ == InputMode::IsearchBackward) {
        matchStart = point;
        matchEnd   = point + query.size();
    }
    else {
        matchEnd   = point;
        matchStart = (point >= query.size()) ? point - query.size() : 0;
    }

    return byteOffset >= matchStart && byteOffset < matchEnd;
}

} // namespace ned::ui
