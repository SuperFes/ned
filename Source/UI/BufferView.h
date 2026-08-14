//
// Renders a Buffer's visible lines and forwards key input to a Dispatcher.
// The core "actual editor" widget.
//
// Also drives interactive sub-sessions (isearch, query-replace, quit
// confirmation, find-file, switch-to-buffer) directly: while one is active,
// key_press routes to it instead of Dispatcher. There is no separate
// minibuffer widget for this -- live status text is written into the same
// shared status-message string EchoArea already displays.
//

#ifndef NED_UI_BUFFERVIEW_H
#define NED_UI_BUFFERVIEW_H

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <ox/ox.hpp>

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
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "Theme.h"

namespace ned::ui {

class BufferView : public ox::Widget {
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

    void paint(ox::Canvas c) override;
    void key_press(ox::Key key) override;
    void timer() override; // periodic scratch auto-save tick -- see StartAutoSaveTimer

    // Click moves point (and clears any selection); click-and-drag extends a
    // selection from the press position; wheel scrolls the viewport without
    // moving point. All three are no-ops (except wheel) during an isearch/
    // query-replace session -- clicking around mid-session doesn't have a
    // sensible meaning, the same reason key_press routes elsewhere then too.
    void mouse_press(ox::Mouse mouse) override;
    void mouse_release(ox::Mouse mouse) override; // no behavior yet -- logged only, see LogMouseEvent
    void mouse_move(ox::Mouse mouse) override;
    void mouse_wheel(ox::Mouse mouse) override;

    // Scroll-bar follow-up: topLine_ read/write for an externally-owned
    // ox::ScrollBar to sync against. SetTopLine clamps the same way
    // mouse_wheel already does. SetScrollBar registers the bar paint() keeps
    // in sync each frame (scrollable_length/position/item_visual_length) --
    // nullptr (the default) means no scroll bar is wired in, a no-op in
    // paint(). The reverse direction (bar drag/wheel -> BufferView) is wired
    // by the caller connecting ox::ScrollBar::on_scroll to SetTopLine
    // directly; BufferView has no dependency on sl::Signal for that.
    [[nodiscard]] std::size_t TopLine() const;
    void                      SetTopLine(std::size_t line);
    void                      SetScrollBar(ox::ScrollBar* scrollBar);

    // Registers the up/down arrow caps flanking the scroll bar so paint()
    // can keep their enabled state in sync each frame: up is enabled only
    // when topLine_ > 0, down only when topLine_ < MaxTopLine() -- both
    // false at once when the whole buffer already fits on screen. Either or
    // both may be nullptr (the default) to opt out.
    void SetScrollArrows(ScrollArrowButton* up, ScrollArrowButton* down);

    // Registers the left-side project tree so toggle-project-sidebar
    // (project-sidebar follow-up) can flip its ox::Widget::active flag;
    // nullptr (the default) means the toggle command is a no-op.
    void SetProjectSidebar(ProjectSidebar* sidebar);

    // Registers the ox::Row containing ProjectSidebar (round-2 sidebar
    // follow-up) so toggling the sidebar's active flag can force that Row to
    // immediately re-run its own resize() and reclaim/return BufferView's
    // width -- flipping .active alone is *not* enough: TermOx only
    // recomputes a layout's child widths/positions in response to an actual
    // terminal resize event (see Row::resize/distribute_length in the
    // vendored layout.hpp), never automatically on a plain field write, so
    // without this the freed column stays a dead gap until the user happens
    // to resize their terminal. resize()'s Area parameter is discarded by
    // Row's own implementation (each child's *own* previous size is what
    // matters, captured internally), so passing the Row's current size back
    // to itself here is a safe, if slightly unusual-looking, way to request
    // "recompute now" without needing to track a real previous size.
    // nullptr (the default) means toggling only flips the flag, matching the
    // pre-fix (buggy) behavior.
    void SetSidebarRow(ox::Widget* sidebarRow);

    // Entry point for TabBar's close-icon click (tab-close follow-up) --
    // TabBar only ever signals *intent*, the same "mouse-driven widget hands
    // off to BufferView" shape SetProjectSidebar/SetSidebarRow's callers
    // already establish, since only BufferView can drive a keyboard y/n
    // confirmation (TabBar is FocusPolicy::None, it never receives key
    // events). An unmodified buffer closes immediately; a modified one
    // starts a ConfirmCloseBuffer prompt, mirroring ConfirmQuit but scoped
    // to this one buffer rather than every buffer in the list. Closing the
    // last remaining buffer conjures a fresh scratch buffer as its
    // replacement rather than refusing -- BufferList must always have at
    // least one buffer, and there's nothing meaningful to show otherwise,
    // the same call Emacs itself makes for *scratch*. A no-op (reports via
    // statusMessage_ instead of silently doing nothing) if another
    // interactive session is already in progress.
    void RequestCloseBuffer(text::Buffer& buffer);

    // Starts the periodic scratch auto-save timer (auto-saved-scratch-pads
    // follow-up) -- not started automatically at construction, since that
    // would spin up a real background thread for every BufferView built in
    // tests; main.cpp calls this once for the real, running editor, the same
    // "inert until explicitly wired up" pattern SetScrollBar/SetProjectSidebar
    // already establish for other main.cpp-only wiring.
    void StartAutoSaveTimer();

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

    void StartInteractiveSession(editor::InteractiveRequest request);
    void EndInteractiveSession();
    void HandleSearchKey(ox::Key key);
    void HandleQueryReplaceKey(ox::Key key);
    void HandleConfirmQuitKey(ox::Key key);
    void HandlePromptKey(
        ox::Key key);      // shared by FindFile/SwitchToBuffer/ProjectSearch/CreateDirectory/FindScratch -- see prompt_
    void CompletePrompt(); // Tab in HandlePromptKey -- find-file paths, buffer names, or scratch names, by inputMode_
    void HandleProjectReplaceKey(ox::Key key);
    void HandleConfirmCloseBufferKey(ox::Key key); // see RequestCloseBuffer/pendingClose_

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
    void HandleDeleteFileKey(ox::Key key);
    void HandleRenameFileKey(ox::Key key);

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
    // filler rows. Used by both SetTopLine and paint()'s scroll-bar sync, so
    // wheel/scroll-bar-driven scrolling and the bar's own visual range agree
    // on where "the bottom" is.
    [[nodiscard]] std::size_t MaxTopLine() const;

    // Translates an on-screen mouse position into a buffer byte offset,
    // accounting for the current scroll position and the line-number gutter.
    [[nodiscard]] std::size_t ByteOffsetForMouse(ox::Mouse mouse) const;

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
    void LogMouseEvent(std::string_view event, ox::Mouse mouse) const;

    // Highlight-overlay predicates used by paint(); byteOffset is a byte
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
    std::size_t                dragAnchor_ = 0;            // point position at the last mouse_press, for drag-selection
    std::optional<std::string> debugMouseLogPath_;         // see LogMouseEvent
    ox::ScrollBar*             scrollBar_       = nullptr; // see SetScrollBar
    ScrollArrowButton*         scrollUpArrow_   = nullptr; // see SetScrollArrows
    ScrollArrowButton*         scrollDownArrow_ = nullptr;
    ProjectSidebar*            projectSidebar_  = nullptr; // see SetProjectSidebar
    ox::Widget*                sidebarRow_      = nullptr; // see SetSidebarRow

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

    ox::Timer autoSaveTimer_; // see StartAutoSaveTimer

    // Caches mode_.highlight's result across paint() calls (tree-sitter
    // foundation follow-up) -- paint() runs far more often than the buffer's
    // content actually changes (cursor blink, scrolling, mouse move, an
    // unrelated widget repainting), and mode_.highlight can be a real
    // tree-sitter parse + query run, not a free call. Recomputed only when
    // either the active buffer's identity or its Buffer::ContentGeneration()
    // has changed since the last paint() -- a real, measured fix, not a
    // preemptive one: an earlier version recomputed unconditionally every
    // paint() call and regressed a large-JSON [Performance] test to ~217ms
    // per call (10.9s for 50 calls), caught before shipping the same way
    // this project's other perf regressions have been.
    text::Buffer*                      highlightCacheBuffer_     = nullptr;
    std::size_t                        highlightCacheGeneration_ = 0;
    std::vector<editor::HighlightSpan> highlightCacheSpans_;
};

} // namespace ned::ui

#endif // NED_UI_BUFFERVIEW_H
