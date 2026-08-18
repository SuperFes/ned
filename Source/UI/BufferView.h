//
// Renders a Buffer's visible lines and forwards key input to a Dispatcher.
// The core "actual editor" widget.
//
// Also drives interactive sub-sessions (isearch, query-replace, quit
// confirmation, find-file, switch-to-buffer) directly: while one is active,
// key events route to it instead of Dispatcher. There is no separate
// minibuffer widget for this -- live status text is written into the same
// shared status-message string EchoArea already displays.
//

#ifndef NED_UI_BUFFERVIEW_H
#define NED_UI_BUFFERVIEW_H

#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ActiveBuffer.h"
#include "Editor/CodeFold.h"
#include "Editor/Command.h"
#include "Editor/Dispatcher.h"
#include "Editor/IncrementalSearch.h"
#include "Editor/Link.h"
#include "Editor/MinibufferPrompt.h"
#include "Editor/Mode.h"
#include "Editor/Org.h"
#include "Editor/ProjectReplace.h"
#include "Editor/QueryReplace.h"
#include "Editor/Register.h"
#include "ProjectSidebar.h"
#include "ScrollArrowButton.h"
#include "ScrollBar.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "Theme.h"

namespace ned::ui {

class BufferView : public Widget {
  public:
    // statusMessage is where a caught command exception, a command like
    // save-buffer reporting its own outcome, or live isearch/query-replace/
    // prompt status text gets written -- see EchoArea, which displays
    // whatever this currently holds. activeBuffer, mode, and theme must
    // outlive this BufferView (matches how killRing/bufferList/dispatcher/
    // statusMessage are already externally-owned references with the same
    // requirement).
    BufferView(ActiveBuffer& activeBuffer, text::KillRing& killRing, editor::RegisterTable& registers,
               text::BufferList& bufferList, editor::Dispatcher& dispatcher, std::string& statusMessage,
               const editor::Mode& mode, const Theme& theme);

    BufferView(const BufferView&)            = delete;
    BufferView& operator=(const BufferView&) = delete;

    void Paint(Canvas c) override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override; // was FocusPolicy::Strong

    // Local cursor position for the real terminal caret -- was ox::Widget's
    // own `cursor` field. A pure, independent computation, deliberately NOT
    // cached from Paint() -- see the .cpp definition's own comment for why
    // that caused a real, reported one-frame-stale cursor bug.
    [[nodiscard]] std::optional<Point> CursorPosition() const override;

    // Scroll-bar follow-up: topLine_ read/write for an externally-owned
    // ScrollBar to sync against. SetTopLine clamps the same way wheel
    // scrolling already does. SetScrollBar registers the bar Paint() keeps
    // in sync each frame (scrollable_length/position/item_visual_length) --
    // nullptr (the default) means no scroll bar is wired in, a no-op in
    // Paint(). The reverse direction (bar drag/wheel -> BufferView) is wired
    // by the caller connecting ScrollBar::SetOnScroll to SetTopLine
    // directly.
    [[nodiscard]] std::size_t TopLine() const;
    void                      SetTopLine(std::size_t line);
    void                      SetScrollBar(ScrollBar* scrollBar);

    // Registers the up/down arrow caps flanking the scroll bar so Paint()
    // can keep their enabled state in sync each frame: up is enabled only
    // when topLine_ > 0, down only when topLine_ < MaxTopLine() -- both
    // false at once when the whole buffer already fits on screen. Either or
    // both may be nullptr (the default) to opt out.
    void SetScrollArrows(ScrollArrowButton* up, ScrollArrowButton* down);

    // Registers the left-side project tree so toggle-project-sidebar
    // (project-sidebar follow-up) can flip its Widget::active flag; nullptr
    // (the default) means the toggle command is a no-op. Unlike the
    // pre-migration version, flipping .active alone is now sufficient --
    // no SetSidebarRow/forced-reflow equivalent is needed (FTXUI rebuilds
    // its element tree fresh every frame; confirmed during the TermOx ->
    // FTXUI migration, see ROADMAP.md).
    void SetProjectSidebar(ProjectSidebar* sidebar);

    // Entry point for TabBar's close-icon click (tab-close follow-up) --
    // TabBar only ever signals *intent*, the same "mouse-driven widget hands
    // off to BufferView" shape SetProjectSidebar's callers already
    // establish, since only BufferView can drive a keyboard y/n
    // confirmation (TabBar takes no keyboard focus). An unmodified buffer
    // closes immediately; a modified one starts a ConfirmCloseBuffer
    // prompt, mirroring ConfirmQuit but scoped to this one buffer rather
    // than every buffer in the list. Closing the last remaining buffer
    // conjures a fresh scratch buffer as its replacement rather than
    // refusing -- BufferList must always have at least one buffer, and
    // there's nothing meaningful to show otherwise, the same call Emacs
    // itself makes for *scratch*. A no-op (reports via statusMessage_
    // instead of silently doing nothing) if another interactive session is
    // already in progress.
    void RequestCloseBuffer(text::Buffer& buffer);

    // Window-splitting follow-up: called with the same InteractiveRequest
    // whenever StartInteractiveSession sees one of the five structural
    // window-management values (SplitBelow/SplitRight/DeleteWindow/
    // DeleteOtherWindows/OtherWindow) -- these operate above the level of a
    // single BufferView, so unlike every other InteractiveRequest this
    // class doesn't act on them itself, it just forwards. Mirrors
    // SetProjectSidebar/SetOnCloseRequest's own "connect after construction,
    // unset is a safe no-op" convention exactly. WindowManager (the owner of
    // however many BufferViews exist) is the intended registrant.
    void SetOnWindowRequest(std::function<void(editor::InteractiveRequest)> handler);

    // Window-splitting follow-up: called from CloseBufferNow, before the
    // buffer is actually erased from bufferList_, with the buffer that's
    // about to close -- so a multi-pane owner can retarget any *other* pane
    // whose ActiveBuffer also pointed at it (this BufferView's own
    // activeBuffer_ is already handled internally by CloseBufferNow). Unset
    // is a safe no-op, matching every other Set* hook here.
    void SetOnBufferClosed(std::function<void(text::Buffer&)> handler);

  private:
    enum class InputMode { Normal,
                           IsearchForward,
                           IsearchBackward,
                           QueryReplace,
                           ConfirmQuit,
                           FindFile,
                           SwitchToBuffer,
                           ProjectSearch,
                           ProjectReplace,
                           ConfirmCloseBuffer,
                           CreateDirectory,
                           DeleteFile,
                           RenameFile,
                           FindScratch,
                           ExecuteCommand,
                           PointToRegister,
                           JumpToRegister,
                           CopyToRegister,
                           InsertRegister,
                           StringRectangle,
                           SetHeadlineTags };

    enum class DeleteFileStage { EnteringPath,
                                 Confirming };
    enum class RenameFileStage { EnteringSource,
                                 EnteringDestination };

    // Builds a fresh CommandContext from current member state -- matches
    // CommandContext's own documented contract ("built fresh per invocation
    // ... never stored"). Only used for the normal Dispatcher::Feed path;
    // the interactive sub-sessions below read/write buffer/statusMessage_
    // directly, since they don't go through CommandRegistry.
    [[nodiscard]] editor::CommandContext MakeContext();

    // Keyboard/mouse handling split out of OnEvent for readability -- was
    // key_press/mouse_press/mouse_move/mouse_release/mouse_wheel.
    bool OnKeyEvent(ftxui::Event event);
    bool OnMouseEvent(ftxui::Event event);

    void StartInteractiveSession(editor::InteractiveRequest request);
    void EndInteractiveSession();
    void HandleSearchKey(const editor::KeyChord& chord);
    void HandleQueryReplaceKey(const editor::KeyChord& chord);
    void HandleConfirmQuitKey(const editor::KeyChord& chord);
    void HandlePromptKey(
        const editor::KeyChord&
            chord);        // shared by FindFile/SwitchToBuffer/ProjectSearch/CreateDirectory/FindScratch/StringRectangle -- see prompt_
    void CompletePrompt(); // Tab in HandlePromptKey -- find-file paths, buffer names, or scratch names, by inputMode_
    void HandleProjectReplaceKey(const editor::KeyChord& chord);
    void HandleConfirmCloseBufferKey(const editor::KeyChord& chord); // see RequestCloseBuffer/pendingClose_

    // Both project-file-ops follow-up, both a simple two-stage flow driven
    // directly on BufferView (no dedicated state-machine class, unlike
    // QueryReplace/ProjectReplace -- these are linear with no branching
    // decision beyond the final y/n, closer in shape to
    // ConfirmCloseBuffer/pendingClose_ than to anything QueryReplace-sized):
    // HandleDeleteFileKey prompts for a path (deleteStage_ ==
    // EnteringPath), then, once it's confirmed to exist, re-purposes
    // statusMessage_ for a y/n confirmation (deleteStage_ == Confirming) --
    // mirroring HandleConfirmCloseBufferKey/HandleConfirmQuitKey's own
    // y/n shape exactly, since deleting a file is just as irreversible.
    // HandleRenameFileKey prompts for the source path (renameStage_ ==
    // EnteringSource), then re-emplaces prompt_ for the destination
    // (renameStage_ == EnteringDestination) and performs the rename on the
    // second Enter; if the renamed file is the currently active buffer,
    // Buffer::SetPath/Rename follow it to the new location rather than
    // leaving that buffer pointing at a now-nonexistent path.
    void HandleDeleteFileKey(const editor::KeyChord& chord);
    void HandleRenameFileKey(const editor::KeyChord& chord);

    // point-to-register/jump-to-register/copy-to-register/insert-register
    // follow-up: one shared method for all four (mirrors HandlePromptKey's
    // own "several related modes, one handler that switches on inputMode_
    // internally" shape) rather than four near-duplicate methods, since each
    // operation's own interaction shape is identical -- read exactly one
    // more character (the register name, no MinibufferPrompt needed, there's
    // nothing to accumulate), act, end the session. Only what happens with
    // that character differs per inputMode_. See Editor/Register.h for where
    // the actual register storage lives (registers_ below).
    void HandleRegisterKey(const editor::KeyChord& chord);

    // execute-extended-command follow-up (M-x): given its own dedicated
    // method rather than folded into HandlePromptKey, since HandlePromptKey's
    // Enter-branch has an unconditional catch-all else currently reached only
    // by FindScratch -- silently misrouting a new mode into it would be a
    // real bug -- and because its key semantics (Up/Down candidate
    // selection, re-ranking on every keystroke) genuinely differ from every
    // HandlePromptKey mode's shared shape, the same reason
    // DeleteFile/RenameFile already get their own methods. Prompts for a
    // command name, fuzzy-matched (Editor/FuzzyMatch.h) against
    // dispatcher_.Registry().Names() and re-ranked on every keystroke; Enter
    // invokes whichever ranked candidate is currently selected. Shown inline
    // via statusMessage_ using the same "label + text + {candidates}"
    // convention CompletePrompt already established, since this codebase has
    // no floating/popup widget concept to show a real dropdown in (see
    // ProjectSidebar's own context-menu descoping history).
    void HandleExecuteCommandKey(const editor::KeyChord& chord);

    // Refreshes statusMessage_ from the current prompt_ text and
    // executeCommandSelection_ -- shared by StartInteractiveSession's
    // ExecuteCommand case and every branch of HandleExecuteCommandKey that
    // changes either one.
    void RefreshExecuteCommandStatus();

    // Shared by OnKeyEvent's Normal-mode tail (Dispatcher::Feed) and
    // HandleExecuteCommandKey's Enter branch (CommandRegistry::Invoke by
    // name): applies the two dispatch-level side effects a command can
    // request -- context.quit (exit the app) and a chained
    // context.interactiveRequest (immediately start that request's own
    // session, letting an M-x-invoked command like find-file chain straight
    // into its own prompt) -- and catches std::exception into
    // statusMessage_, regardless of how the command was found. invoke is the
    // actual dispatch call, run inside the try.
    void RunCommandAndHandleOutcome(editor::CommandContext& context, const std::function<void()>& invoke);

    // kmacro-end-or-call-macro follow-up: replays dispatcher_.LastMacro(),
    // one chord at a time, each through a fresh MakeContext() +
    // RunCommandAndHandleOutcome -- exactly what a real keystroke does.
    // Reports "No keyboard macro has been recorded yet." via statusMessage_
    // if nothing's been recorded (mirrors delete-window's own "Cannot delete
    // the only window." can't-do-that-right-now convention). Stops early,
    // leaving the rest of the macro un-replayed, the instant a replayed
    // command opens an interactive session (inputMode_ != Normal) or
    // requests quit -- a macro's own recording never captured what happens
    // *inside* such a session (see Dispatcher::StartRecording's own doc
    // comment), so blindly continuing to feed the macro's remaining chords
    // through Dispatcher::Feed underneath a now-live session would be
    // feeding them to the wrong place entirely; this leaves that session
    // genuinely live for the user to finish by hand instead.
    void ReplayMacro();

    // narrow-to-region/widen follow-up: keeps point confined to a narrowed
    // buffer's own NarrowedRange() -- a no-op if the active buffer isn't
    // narrowed. Called once before each of OnKeyEvent's own return
    // statements, and once per chord inside ReplayMacro's own loop -- these
    // are the two real entry points for anything that could move point
    // (every key-driven Handle*Key method, plus macro replay, which
    // bypasses OnKeyEvent entirely). Most of Buffer's own motion methods
    // mutate Point_ by direct assignment rather than through the one public
    // SetPoint() setter (confirmed by reading Buffer.cpp directly), so
    // clamping only inside SetPoint would silently miss most of them --
    // this is the actual, correct, centralized place instead. Deliberately
    // not inside Paint(): giving rendering code a buffer-mutating side
    // effect would make repeated Paint() calls with no intervening input
    // non-idempotent.
    void ClampPointToNarrowing();

    // The actual close: removes buffer from bufferList_ and, if it was the
    // active one, switches activeBuffer_ to whatever remains (the first
    // other buffer in list order -- there is no "most recently used" concept
    // to prefer here yet). Caller (RequestCloseBuffer or
    // HandleConfirmCloseBufferKey) is responsible for the modified-buffer
    // confirmation decision; this always closes unconditionally.
    void CloseBufferNow(text::Buffer& buffer);

    // Builds a results buffer (path:line: text per line, name uniquified
    // Emacs-style like any other buffer) from matches and switches to it --
    // shared by project-search's own results view and project-replace's
    // preview, which deliberately shows the same file/line detail rather
    // than a bare count (see ROADMAP.md's project-replace notes).
    void BuildResultsBuffer(const std::vector<editor::SearchMatch>& matches, const std::string& name);

    // VisitSearchResult (project-search follow-up): a one-shot direct action,
    // not a prompt session -- doesn't touch inputMode_. Parses the current
    // line for a "path:line:" prefix (the exact format project-search writes
    // into its results buffer) and, if it matches, opens that file and jumps
    // to the target line. A silent no-op on any line that doesn't match,
    // which is what makes it safe to bind globally rather than gating it on
    // which buffer happens to be active.
    void VisitSearchResult();

    // Links follow-up: another one-shot direct action, same shape as
    // VisitSearchResult -- doesn't touch inputMode_. In an org-mode buffer,
    // tries org::LinkAtPoint first (an internal "*Heading" target jumps
    // point in-buffer via org::FindHeadlineByTitle; any other target is
    // classified and handed to OpenDetectedLink below, reusing the same
    // logic the generic path uses); falls back to (or, outside org-mode,
    // goes straight to) editor::link::DetectLinkAtPoint. Reports "No link at
    // point." via statusMessage_ if nothing is found either way.
    void OpenLinkAtPoint();
    // The shared open/report tail both OpenLinkAtPoint paths above funnel
    // into: a Url opens via editor::link::OpenUrl; a File is resolved via
    // editor::link::ResolveFileLink against the active buffer's own
    // containing directory (falling back to editor::ProjectRoot() when the
    // buffer has no path, e.g. a scratch buffer) and, if found, opened the
    // same way VisitSearchResult opens a file (bufferList_.OpenOrCreateFile +
    // activeBuffer_.Set).
    void OpenDetectedLink(const editor::link::DetectedLink& detected);

    // Adjusts the viewport (if needed) so point's line is visible.
    void ScrollToShowPoint();

    // Re-validates topLine_ via ScrollToShowPoint() whenever the active
    // buffer's identity has changed since the last call -- see
    // topLineValidatedBuffer_'s own doc comment for why this exists.
    // Deliberately doesn't reset topLine_ to 0 first: ScrollToShowPoint()
    // is already safe to call with a stale, possibly out-of-range topLine_
    // left over from the previous buffer (its own "point is above topLine_"
    // branch handles that unconditionally), and leaving topLine_ untouched
    // when it happens to already show the new buffer's point is a nicer
    // switch between two similarly long buffers than always jumping back to
    // the top. Called first thing in Paint(), before topLine_ is used for
    // anything.
    void EnsureTopLineValidForActiveBuffer();

    // The largest valid topLine_: the buffer's last line stops exactly at
    // the bottom of the viewport rather than scrolling past it into blank
    // filler rows. Used by both SetTopLine and Paint()'s scroll-bar sync, so
    // wheel/scroll-bar-driven scrolling and the bar's own visual range agree
    // on where "the bottom" is. narrow-to-region/widen follow-up: when the
    // active buffer is narrowed, this is computed against the narrowed
    // range's own line span instead of the whole buffer -- see
    // NarrowedLineRange.
    [[nodiscard]] std::size_t MaxTopLine() const;

    // narrow-to-region/widen follow-up: {0, Content().LineCount()} if the
    // active buffer isn't narrowed, otherwise the narrowed range's own
    // [startLine, endLine) span (endLine exclusive) -- shared by MaxTopLine,
    // SetTopLine, and Paint()'s own "blank past this line" cutoff so all
    // three agree on exactly the same bounds.
    [[nodiscard]] std::pair<std::size_t, std::size_t> NarrowedLineRange() const;

    // Translates an on-screen (LOCAL to this widget) mouse position into a
    // buffer byte offset, accounting for the current scroll position and the
    // line-number gutter.
    [[nodiscard]] std::size_t ByteOffsetForPoint(Point at) const;

    // Width in columns of the line-number gutter (digits needed for the
    // buffer's last line number, plus one separating column). Always
    // present -- there's no toggle to hide it yet.
    [[nodiscard]] std::size_t GutterWidth() const;

    // Diagnostic aid, opt-in via $NED_DEBUG_MOUSE (a file path to append
    // to): logs the raw event plus current point/mark/topLine_/size at the
    // top of every mouse handler call, before any of it can be mutated by
    // that call. Added to chase down an intermittent, real-terminal-only
    // (not reproducible headlessly) selection-highlight rendering glitch --
    // see ROADMAP.md. A no-op, effectively free, when the env var is unset.
    void LogMouseEvent(std::string_view event, const ftxui::Mouse& mouse) const;

    // Org-mode fold/unfold follow-up: everywhere in this class that used to
    // reason in raw "buffer line" units now has to skip lines an active
    // Org fold hides (org::FoldedLineRanges) -- these five are the single
    // shared vocabulary for that, used by Paint()'s row loop, CursorPosition(),
    // ScrollToShowPoint(), TopLine()/SetTopLine()/MaxTopLine(), and
    // ByteOffsetForPoint() alike, so none of them can disagree about which
    // lines are actually visible.
    //
    // EnsureHiddenLineRangesCache recomputes hiddenLineRanges_ via
    // org::FoldedLineRanges only when the active buffer pointer, its
    // ContentGeneration(), or its FoldGeneration() have changed since the
    // last call -- and skips calling FoldedLineRanges entirely when
    // buffer.FoldMarkers() is empty, so every buffer that's never had a
    // fold touched (every non-Org buffer, and Org buffers before the first
    // TAB) pays exactly zero extra cost, not just an amortized-cheap one.
    // Marked const/mutable-backed since CursorPosition()/ByteOffsetForPoint()
    // (both const) need a fresh cache just as much as Paint() (non-const)
    // does, and all three can run in either order within a frame.
    void               EnsureHiddenLineRangesCache() const;
    // generic-code-folding follow-up: recomputes foldableBlocksCache_ via
    // codefold::FoldableBlocks(mode_, buffer.Text()) only when the active
    // buffer's identity or its ContentGeneration() changed since the last
    // call -- mirrors highlightCacheBuffer_'s own caching shape. Leaves
    // foldableBlocksCache_ empty (and skips calling mode_.fold entirely)
    // whenever mode_.fold itself is empty or editor::CodeFoldingEnabled()
    // is false, which is also exactly the "no gutter affordance" condition
    // Paint()/GutterWidth()/OnMouseEvent all check.
    void EnsureFoldableBlocksCache() const;
    // depth-aware-fold-gutter follow-up: calls EnsureFoldableBlocksCache()
    // first, then (re)derives foldGutterEntries_/foldGutterLineRangesByColumn_
    // from its result -- gated on the buffer plus BOTH ContentGeneration()
    // and FoldGeneration(), since which entries are "expanded" (and so get a
    // line drawn) depends on live FoldMarker state, not just content. See
    // the member declarations' own doc comment for the full history behind
    // exactly what's cached here and why.
    void EnsureFoldGutterCache() const;
    // status-gutter unsaved-change-indicator follow-up: (re)derives
    // unsavedChangeLineRanges_ from buffer.UnsavedChangeRanges() -- gated
    // on the buffer plus BOTH ContentGeneration() and
    // UnsavedChangeGeneration(), since a save clears the ranges (bumping
    // the latter) without necessarily changing content. Called
    // unconditionally every Paint(), unlike EnsureFoldGutterCache -- the
    // status column isn't gated on mode_.fold, every buffer gets one
    // regardless of language.
    void EnsureUnsavedChangeCache() const;
    [[nodiscard]] bool IsLineHidden(std::size_t line) const;
    // `line` if already visible, else the first visible line >= line
    // (capped at limit).
    [[nodiscard]] std::size_t NextVisibleLine(std::size_t line, std::size_t limit) const;
    // Steps forward `count` visible lines from an already-visible `line`,
    // capped at limit.
    [[nodiscard]] std::size_t AdvanceVisibleLines(std::size_t line, std::size_t count, std::size_t limit) const;
    // Count of visible (non-hidden) lines in [startLine, endLineExclusive).
    [[nodiscard]] std::size_t VisibleLineCountBetween(std::size_t startLine, std::size_t endLineExclusive) const;

    // Highlight-overlay predicates used by Paint(); byteOffset is a byte
    // offset into the buffer's current content.
    [[nodiscard]] bool InSelection(std::size_t byteOffset) const;
    [[nodiscard]] bool InIsearchMatch(std::size_t byteOffset) const;

    ActiveBuffer&          activeBuffer_;
    text::KillRing&        killRing_;
    editor::RegisterTable& registers_;
    text::BufferList&      bufferList_;
    editor::Dispatcher&    dispatcher_;
    std::string&           statusMessage_;
    const editor::Mode&    mode_;
    const Theme&           theme_;

    std::size_t topLine_ = 0; // first visible buffer line (0-indexed)
    // The buffer topLine_ was last validated against -- topLine_ itself is
    // BufferView-level state, not per-buffer, so switching which buffer is
    // active (TabBar's own click handler calls ActiveBuffer::Set() directly,
    // with no relationship to BufferView at all, but every other switch path
    // -- find-file, switch-to-buffer, ProjectSidebar's click-to-open, etc. --
    // is exactly as disconnected from topLine_ in principle) can otherwise
    // leave it pointing at a scroll position that doesn't exist in the newly
    // active buffer at all -- a real reported bug (switching from a long
    // file scrolled well past a short file's own last line rendered nothing
    // but blank rows), not hypothetical. EnsureTopLineValidForActiveBuffer,
    // called first thing in Paint() the same way highlightCacheBuffer_/
    // hiddenLineRangesCacheBuffer_/linkCacheBuffer_ already detect "the
    // active buffer changed since I last looked," re-validates topLine_ via
    // ScrollToShowPoint() whenever this doesn't match the buffer Paint() is
    // about to render -- see EnsureTopLineValidForActiveBuffer's own
    // declaration below, alongside this class's other private methods, for
    // why that's sufficient without also needing to reset topLine_ first.
    // Seeded to the buffer active at construction time (not nullptr) so the
    // very first Paint() call is never itself mistaken for a switch -- see
    // the constructor's own comment for the real regression that caught.
    text::Buffer* topLineValidatedBuffer_ = nullptr;

    std::size_t                dragAnchor_ = 0;            // point position at the last mouse press, for drag-selection
    std::optional<std::string> debugMouseLogPath_;         // see LogMouseEvent
    ScrollBar*                 scrollBar_       = nullptr; // see SetScrollBar
    ScrollArrowButton*         scrollUpArrow_   = nullptr; // see SetScrollArrows
    ScrollArrowButton*         scrollDownArrow_ = nullptr;
    ProjectSidebar*            projectSidebar_  = nullptr; // see SetProjectSidebar

    InputMode                                inputMode_ = InputMode::Normal;
    std::optional<editor::IncrementalSearch> search_;
    std::optional<editor::QueryReplace>      queryReplace_;
    std::optional<editor::MinibufferPrompt>  prompt_; // FindFile/SwitchToBuffer/ProjectSearch, distinguished by inputMode_
    std::optional<editor::ProjectReplace>    projectReplace_;
    text::Buffer*                            pendingClose_ = nullptr; // buffer awaiting y/n in ConfirmCloseBuffer

    DeleteFileStage       deleteStage_ = DeleteFileStage::EnteringPath;
    std::filesystem::path deleteTarget_; // path awaiting y/n in DeleteFileStage::Confirming

    RenameFileStage       renameStage_ = RenameFileStage::EnteringSource;
    std::filesystem::path renameSource_; // path entered in RenameFileStage::EnteringSource

    // execute-extended-command follow-up: index into the ranked candidate
    // list FuzzyFilterAndRank produces fresh from prompt_->Text() on every
    // keystroke/render -- the ranked list itself isn't cached as a member,
    // it's cheap to recompute (a few dozen short strings), matching this
    // codebase's established "recompute, don't cache" convention for cheap
    // per-frame/per-keystroke work (e.g. ScrollArrowButton, WindowManager's
    // own tree walks).
    std::size_t executeCommandSelection_ = 0;

    // kmacro-end-or-call-macro follow-up: reentrancy guard for ReplayMacro --
    // a macro can never structurally contain a call to replay itself (see
    // that method's own doc comment for why), but this is kept anyway as a
    // cheap, unconditional backstop rather than resting entirely on that
    // argument.
    bool replayingMacro_ = false;

    // Window-splitting follow-up: see SetOnWindowRequest/SetOnBufferClosed.
    std::function<void(editor::InteractiveRequest)> onWindowRequest_;
    std::function<void(text::Buffer&)>              onBufferClosed_;

    // Caches mode_.highlight's result across Paint() calls (tree-sitter
    // foundation follow-up) -- Paint() runs far more often than the buffer's
    // content actually changes (cursor blink, scrolling, mouse move, an
    // unrelated widget repainting), and mode_.highlight can be a real
    // tree-sitter parse + query run, not a free call. Recomputed only when
    // either the active buffer's identity or its Buffer::ContentGeneration()
    // has changed since the last Paint() -- a real, measured fix, not a
    // preemptive one: an earlier version recomputed unconditionally every
    // Paint() call and regressed a large-JSON [Performance] test to ~217ms
    // per call (10.9s for 50 calls), caught before shipping the same way
    // this project's other perf regressions have been.
    text::Buffer*                      highlightCacheBuffer_     = nullptr;
    std::size_t                        highlightCacheGeneration_ = 0;
    std::vector<editor::HighlightSpan> highlightCacheSpans_;

    // generic-code-folding follow-up: caches mode_.fold's result across
    // Paint() calls, same shape/reasoning as highlightCacheBuffer_ above --
    // consumed both for the gutter's ▸/▾ rendering and (passed into
    // codefold::FoldedLineRanges) for EnsureHiddenLineRangesCache, so
    // mode_.fold is never called more than once per actually-changed
    // Paint() call. Empty whenever mode_.fold itself is empty or
    // editor::CodeFoldingEnabled() is false -- see EnsureFoldableBlocksCache.
    // Mutable for the same const-query-methods reason
    // hiddenLineRangesCacheBuffer_ already is (EnsureHiddenLineRangesCache,
    // a const method, needs a fresh cache too).
    mutable text::Buffer*                                   foldableBlocksCacheBuffer_     = nullptr;
    mutable std::size_t                                     foldableBlocksCacheGeneration_ = 0;
    mutable std::vector<std::pair<std::size_t, std::size_t>> foldableBlocksCache_;
    // depth-aware-fold-gutter follow-up: a small, fixed number of gutter
    // columns (not a viewport-dependent width -- an explicit user choice,
    // so the gutter's own size never jumps around while scrolling past a
    // deeply nested region) reserved for tracing a fold region's extent,
    // one column per nesting level, deeper levels sharing the innermost
    // column.
    static constexpr int kMaxFoldDepthColumns = 4;

    // status/line-number-spacing follow-up: the gutter's own left-to-right
    // layout, left to right -- [status][gap][digits][gap][fold]. kStatusWidth
    // is always reserved (every buffer gets a status column regardless of
    // mode/language); kLineNumberGap appears on BOTH sides of the digits
    // (an explicit user request -- "ensure the line number gutter has an
    // empty space on either side"), not just the trailing one this gutter
    // used to have alone.
    static constexpr std::size_t kStatusWidth   = 1;
    static constexpr std::size_t kLineNumberGap = 1;

    struct FoldGutterEntry {
        std::size_t headerLine;
        std::size_t closerLine; // inclusive
        std::size_t blockStart; // FoldMarker key
        int         column;    // min(depth, kMaxFoldDepthColumns - 1)
    };

    // Derived from foldableBlocksCache_ whenever it's recomputed, but gated
    // on BOTH ContentGeneration and FoldGeneration (mirroring
    // hiddenLineRangesCacheContentGeneration_/hiddenLineRangesCacheFoldGeneration_'s
    // own dual-generation pattern below) rather than foldableBlocksCache_'s
    // own content-only gate: foldGutterLineRangesByColumn_ depends on which
    // blocks are currently collapsed/expanded (FoldMarker state), which
    // changes independently of buffer content. Built once per actually-
    // stale Paint() call, O(blocks) with one allocation each -- the same
    // "cache the derived structure, don't rebuild it every Paint() call, and
    // don't reach for a per-element-allocating container under ASan"
    // discipline foldableBlocksCache_'s own doc comment already documents
    // finding the hard way for this exact code path.
    mutable text::Buffer*                    foldGutterCacheBuffer_            = nullptr;
    mutable std::size_t                      foldGutterCacheContentGeneration_ = 0;
    mutable std::size_t                      foldGutterCacheFoldGeneration_    = 0;
    mutable std::vector<FoldGutterEntry>     foldGutterEntries_;                  // sorted by headerLine (free -- blocks arrive startByte-sorted)
    mutable std::array<std::vector<std::pair<std::size_t, std::size_t>>, kMaxFoldDepthColumns>
        foldGutterLineRangesByColumn_; // EXPANDED entries only, [headerLine+1, closerLine+1) per column

    // status-gutter unsaved-change-indicator follow-up: converts
    // buffer.UnsavedChangeRanges()' byte ranges to merged, sorted
    // [startLine, endLineExclusive) line ranges for the status column's
    // rendering -- gated on BOTH ContentGeneration() and
    // UnsavedChangeGeneration() (mirrors foldGutterCacheBuffer_'s own
    // dual-generation shape just above), since an edit bumps both but a
    // save only bumps the latter (clearing the ranges without otherwise
    // touching content). Unlike the fold-depth columns, these ranges are
    // flat and disjoint by construction (no nesting concept here at all),
    // so rendering only ever needs a binary search against this cache, no
    // streaming stack state.
    mutable text::Buffer*                             unsavedChangeCacheBuffer_            = nullptr;
    mutable std::size_t                               unsavedChangeCacheContentGeneration_ = 0;
    mutable std::size_t                               unsavedChangeCacheGeneration_        = 0;
    mutable std::vector<std::pair<std::size_t, std::size_t>> unsavedChangeLineRanges_;

    // Org-mode fold/unfold follow-up: see EnsureHiddenLineRangesCache's own
    // doc comment above. mutable because CursorPosition()/ByteOffsetForPoint()
    // (both const) refresh it too, the same "cache read by const query
    // methods" shape highlightCacheBuffer_ would also need if any const
    // method ever read it (none currently do).
    mutable text::Buffer*                                    hiddenLineRangesCacheBuffer_            = nullptr;
    mutable std::size_t                                      hiddenLineRangesCacheContentGeneration_ = 0;
    mutable std::size_t                                      hiddenLineRangesCacheFoldGeneration_    = 0;
    mutable std::vector<std::pair<std::size_t, std::size_t>> hiddenLineRanges_;

    // Links follow-up: caches org::ParseLinks's result across Paint()/
    // CursorPosition()/ByteOffsetForPoint() calls, same shape/reasoning as
    // highlightCacheBuffer_ above. EnsureLinkCache clears linkCache_ and
    // returns immediately whenever mode_.name != "org-mode" -- a single
    // string compare, cheaper even than FoldMarkers().empty()'s check, so
    // every non-Org buffer (the common case across the whole editor) never
    // calls org::ParseLinks at all. Mutable for the same const-query-methods
    // reason hiddenLineRangesCacheBuffer_ already is.
    mutable text::Buffer*                  linkCacheBuffer_     = nullptr;
    mutable std::size_t                    linkCacheGeneration_ = 0;
    mutable std::vector<editor::org::Link> linkCache_;

    void EnsureLinkCache() const;
};

} // namespace ned::ui

#endif // NED_UI_BUFFERVIEW_H
