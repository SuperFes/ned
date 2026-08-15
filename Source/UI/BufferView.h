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

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/screen_interactive.hpp>

#include "ActiveBuffer.h"
#include "Editor/Command.h"
#include "Editor/Dispatcher.h"
#include "Editor/IncrementalSearch.h"
#include "Editor/MinibufferPrompt.h"
#include "Editor/Mode.h"
#include "Editor/ProjectReplace.h"
#include "Editor/QueryReplace.h"
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
    BufferView(ActiveBuffer& activeBuffer, text::KillRing& killRing, text::BufferList& bufferList,
               editor::Dispatcher& dispatcher, std::string& statusMessage, const editor::Mode& mode,
               const Theme& theme);
    ~BufferView() override;

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

    // Starts the periodic scratch auto-save timer (auto-saved-scratch-pads
    // follow-up) -- not started automatically at construction, since that
    // would spin up a real background thread for every BufferView built in
    // tests; main.cpp calls this once for the real, running editor, the
    // same "inert until explicitly wired up" pattern SetScrollBar/
    // SetProjectSidebar already establish for other main.cpp-only wiring.
    // Takes the owning ScreenInteractive so the background thread this
    // starts can safely marshal the actual auto-save call back onto the
    // main loop thread via PostEvent (documented thread-safe by FTXUI,
    // confirmed by reading app.cpp, not assumed) rather than touching
    // bufferList_ directly from a second thread.
    void StartAutoSaveTimer(ftxui::ScreenInteractive& screen);

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
                           FindScratch };

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
            chord);        // shared by FindFile/SwitchToBuffer/ProjectSearch/CreateDirectory/FindScratch -- see prompt_
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

    // Adjusts the viewport (if needed) so point's line is visible.
    void ScrollToShowPoint();

    // The largest valid topLine_: the buffer's last line stops exactly at
    // the bottom of the viewport rather than scrolling past it into blank
    // filler rows. Used by both SetTopLine and Paint()'s scroll-bar sync, so
    // wheel/scroll-bar-driven scrolling and the bar's own visual range agree
    // on where "the bottom" is.
    [[nodiscard]] std::size_t MaxTopLine() const;

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

    // Highlight-overlay predicates used by Paint(); byteOffset is a byte
    // offset into the buffer's current content.
    [[nodiscard]] bool InSelection(std::size_t byteOffset) const;
    [[nodiscard]] bool InIsearchMatch(std::size_t byteOffset) const;

    ActiveBuffer&       activeBuffer_;
    text::KillRing&     killRing_;
    text::BufferList&   bufferList_;
    editor::Dispatcher& dispatcher_;
    std::string&        statusMessage_;
    const editor::Mode& mode_;
    const Theme&        theme_;

    std::size_t                topLine_    = 0;            // first visible buffer line (0-indexed)
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

    // Scratch auto-save (see StartAutoSaveTimer) -- a real background
    // std::jthread rather than an FTXUI animation-frame hook (unlike
    // ScrollArrowButton's repeat): this needs to keep firing on a fixed
    // wall-clock interval even while the app is otherwise fully idle (no
    // keyboard/mouse activity, no animation in progress), which an
    // animation-frame-driven approach can only do by continuously
    // requesting new frames forever -- keeping the whole app busy-looping
    // just to watch a clock. jthread's own stop_token makes shutdown
    // automatic and safe on destruction; the thread itself never touches
    // bufferList_ directly, only ever through a PostEvent-marshaled
    // closure run on the main loop thread.
    std::jthread autoSaveThread_;

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
};

} // namespace ned::ui

#endif // NED_UI_BUFFERVIEW_H
