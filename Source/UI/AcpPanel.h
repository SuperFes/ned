//
// ACP chat panel: a dockable OverlayHost widget displaying AcpManager's
// structured transcript (AcpManager::Transcript()), plus its own
// prompt-composition input row -- see AcpManager.h's own header comment for
// why the flat "*acp: <agent>*" output buffer stays untouched alongside
// this. Structurally mirrors TerminalPanel: registered with main.cpp's
// OverlayHost, floats over BufferView without reflowing anything, an
// opaque title row (agent name + state + [x] close) over content rows over
// one input row.
//
// Deliberate v1 cut: permission-prompt *resolution* keystrokes stay in the
// already-shipped BufferView flow (InputMode::AcpPermissionPrompt) -- this
// panel only *displays* the pending prompt (AcpManager::PendingPermissionPrompt())
// read-only. Also no scrollback (TerminalPanel's own documented v1 cut, same
// status here): the content rows show only the tail of the transcript that
// fits.
//

#ifndef NED_UI_ACPPANEL_H
#define NED_UI_ACPPANEL_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Acp/AcpManager.h"
#include "Editor/MinibufferPrompt.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class AcpPanel : public Widget {
  public:
    explicit AcpPanel(const Theme& theme);

    // Connect-after-construction, unset is a safe no-op -- this class's
    // usual convention. Must outlive this AcpPanel.
    void SetAcpManager(editor::acp::AcpManager* acpManager);

    // Invoked when the panel's own [x] close button is clicked -- wired by
    // main.cpp to the same toggle lambda acp-toggle-panel drives, mirroring
    // TerminalPanel::SetOnToggleRequest exactly.
    void SetOnToggleRequest(std::function<void()> onToggle);

    // ACP checkpoint/rewind follow-up: main.cpp's SetOnAcpRewindRequest
    // wiring calls this after showing/focusing the panel (acp-rewind, C-c A
    // r). Replaces the transcript view with a numbered list of past turns
    // (FormatRewindPicker) built from AcpManager::CheckpointCount()/
    // CheckpointAt() -- a digit 1-9 rewinds to that turn (AcpManager::
    // RewindTo) and closes the picker; Escape cancels with no effect. A
    // no-op if no AcpManager is set.
    void OpenRewindPicker();

    // ACP chat-feel round 2 -- panel resize/minimize follow-up. Collapsed()
    // shrinks the panel to a thin title-only strip (ProjectSidebar's own
    // Collapsed() convention: still on screen, still occupying its Box, the
    // ACP session itself keeps running in the background -- distinct from
    // SetOnToggleRequest's full hide). main.cpp's placement lambda consults
    // Collapsed() to shrink the Box accordingly -- but OverlayHost only ever
    // calls that lambda from Show()/Reflow() (Overlay.h's own header
    // comment: "re-derived on every Reflow/Show"), never on every Paint(),
    // so SetCollapsed alone would leave a stale, wrongly-sized Box in place
    // until the next real terminal resize. SetOnCollapseChanged is the fix:
    // fires whenever Collapsed() actually changes, main.cpp's own hook
    // re-invoking overlays.Show(*this) to force the Box to be recomputed
    // immediately.
    [[nodiscard]] bool Collapsed() const;
    void               SetCollapsed(bool collapsed);
    void               SetOnCollapseChanged(std::function<void()> onCollapseChanged);
    void               ToggleCollapsed();

    // The full terminal size, refreshed by main.cpp's placement lambda every
    // Reflow -- needed to convert a resize-drag's pixel delta into an
    // AcpPanelSizePercent() delta (this panel's own Box only ever reports its
    // *own* current size, not the terminal's). Safe to leave unset in tests:
    // resize-dragging is simply inert (BeginResize/UpdateResize compute a
    // zero-sized percent delta) until this is called at least once.
    void SetTerminalSize(Size size);

    void Paint(Canvas canvas) override;
    bool OnEvent(const Event& event) override;

    [[nodiscard]] bool Focusable() const override {
        return true;
    }

  private:
    // ACP round-1-live-validation follow-up: Accent/Hint widen this past the
    // original three buckets so an agent's own words (Accent) and its plan
    // steps (Hint) read as visually distinct from a plain UserMessage echo,
    // not flattened into the same Plain style -- a small, low-risk step
    // toward the transcript reading as less generic/interchangeable across
    // entry kinds. See ROADMAP.md's "AI-assisted editing (ACP) gaps" for the
    // bigger, deliberately-not-attempted-yet ideas this doesn't cover
    // (per-agent theming, distinguishing agent_thought_chunk).
    enum class DisplayStyle { Plain,
                              Dim,
                              Warning,
                              Accent,
                              Hint };

    // ACP Markdown rendering follow-up: a byte range of a *plain, already
    // markup-stripped* DisplayLine::text carrying styling beyond
    // DisplayLine::style itself. startColumn/columnCount are codepoint
    // columns (WordWrap/PaintUtf8Row's own convention), so a span survives
    // being re-based onto whichever WrappedRow it lands in after word-wrap
    // (see SpansForRow). `code` paints a subtle Theme::
    // documentHighlightBackground tint over the existing foreground rather
    // than picking a new foreground outright -- that field's own doc
    // comment's "keep the glyph foreground, overlay only" contract.
    struct InlineSpan {
        int  startColumn;
        int  columnCount;
        bool bold;
        bool code;
    };
    struct DisplayLine {
        std::string             text;
        DisplayStyle            style;
        std::vector<InlineSpan> spans;
    };
    struct InlineMarkdownResult {
        std::string             text;
        std::vector<InlineSpan> spans;
    };

    // Strips **bold**/`code` markup and a leading "- "/"* "/"+ " bullet
    // marker from one logical line of agent-authored text, returning the
    // plain text plus the spans marking what to restyle. Only called for
    // Kind::AgentText/AgentThought/Plan lines (FormatTranscript's own call
    // sites) -- deliberately not run over structural lines this panel
    // itself writes (UserMessage's "> " prefix, ToolCall's "* "/status
    // marker, Permission/SessionEvent chrome), since those aren't Markdown
    // and running this over them would mis-render their own literal
    // "* "/backtick-free punctuation. No nesting, no escapes, unmatched
    // delimiters pass through literally -- deliberately lightweight per
    // ROADMAP.md's own framing of this item.
    [[nodiscard]] static InlineMarkdownResult ApplyInlineMarkdown(std::string_view raw);
    // Re-bases `spans` (a logical DisplayLine's own plain-text column space)
    // onto one WrappedRow's local [0, rowColumnCount) space, clipping
    // anything that doesn't overlap this row at all.
    [[nodiscard]] static std::vector<InlineSpan> SpansForRow(const std::vector<InlineSpan>& spans, int rowStartColumn, int rowColumnCount);
    // PaintUtf8Row's multi-segment sibling: paints `text` in `baseBrush`
    // except where `spans` says otherwise (bold / a documentHighlightBackground
    // tint for inline code). Falls back to a single PaintUtf8Row call when
    // spans is empty -- the common case for every non-agent-authored line.
    void PaintStyledRow(Canvas& canvas, int x, int y, std::string_view text, const std::vector<InlineSpan>& spans, const Brush& baseBrush,
                         int maxColumns) const;

    [[nodiscard]] std::vector<DisplayLine> FormatTranscript(int width) const;
    // ACP checkpoint/rewind follow-up: rewindPickerOpen_'s own content,
    // same DisplayLine/word-wrap pipeline FormatTranscript's result already
    // flows through (Paint()'s content-row loop doesn't care which one fed
    // it). Ignores `width` today (no line here is ever long enough to need
    // it) -- kept as a parameter to match FormatTranscript's own signature.
    [[nodiscard]] std::vector<DisplayLine> FormatRewindPicker(int width) const;
    [[nodiscard]] Brush                    BrushForStyle(DisplayStyle style) const;
    [[nodiscard]] bool                     CloseButtonAt(Point local) const;
    [[nodiscard]] bool                     MinimizeButtonAt(Point local) const;
    void                                   PaintCollapsedStrip(Canvas& canvas, int width, int height) const;

    // Border-drag resize, mirroring ProjectSidebar::BeginResize/UpdateResize/
    // EndResize's exact shape -- Begin anchors the drag's start point and
    // starting size-percent, Update recomputes a fresh percent from the
    // drag's *total* displacement from that anchor every move event (not a
    // per-event delta -- ProjectSidebar's own comment explains why: once a
    // growing drag crosses into a sibling widget's territory, there's no
    // single consistent "previous event" to diff against). Percent is
    // applied live via SetAcpPanelSizePercent on every Update, not just on
    // End -- unlike ProjectSidebar's width_, AcpPanelSizePercent() already
    // *is* the one live value the placement lambda reads every frame, so
    // there's no separate "committed" step to wire.
    void BeginResize(Point globalMouse);
    void UpdateResize(Point globalMouse);
    void EndResize();

    // History recall (Up/Down in the composer, shell-style) -- deliberately
    // has no storage of its own: it re-derives the list of past prompts from
    // acpManager_->Transcript()'s own Kind::UserMessage entries fresh every
    // time rather than keeping a separate duplicate list, so it can never
    // drift from what the transcript actually shows. historyIndex_ is the
    // index into that filtered list currently displayed (nullopt = editing
    // the live, unsent draft, saved in historyDraft_ the moment browsing
    // starts). Deliberate v1 simplification: editing a recalled entry's text
    // and then pressing Up/Down again discards those local edits rather than
    // saving them back into the list -- real shells do something fancier
    // here, not attempted.
    void HistoryPrevious();
    void HistoryNext();

    // @-style file-mention autocomplete follow-up: re-derives mention state
    // purely from (prompt_.Text(), prompt_.CursorByteOffset()) -- called
    // after every composer edit (character insert, backspace/delete, cursor
    // motion, history recall) rather than tracked incrementally, so it can
    // never drift from what's actually in the composer. Opens whenever the
    // cursor sits inside/just past a word that starts with '@' (the word
    // being the run of non-whitespace immediately before the cursor, so
    // "foo@bar" mid-word never triggers -- matches Slack/GitHub/Discord's
    // own "@ must start a word" convention); closes otherwise. Lazily
    // (re)populates mentionCandidates_ only on the closed->open transition,
    // not per keystroke -- ProjectFindFile's own "recursive walk once per
    // session" precedent (BufferView.cpp's InteractiveRequest::
    // ProjectFindFile case), since re-walking the whole project tree on
    // every typed character would be wasteful.
    void RefreshMentionState();
    void RefreshMentionCandidates();
    // Splices the currently-selected ranked candidate into the composer in
    // place of "@" + the typed query, replacing [mentionStartByte_,
    // cursorByteOffset) with "@<path> " -- MinibufferPrompt::SetText's own
    // documented "cursor moves to the end of the replacement" behavior
    // applies here too (same as Tab-completion elsewhere in this codebase),
    // so a mention accepted with trailing text already typed after the
    // cursor loses that trailing text's own cursor position, a deliberate,
    // pre-existing SetText limitation, not a new one.
    void AcceptMentionCandidate();
    // mentionPickerOpen_'s own content, FormatRewindPicker's shape: a
    // fuzzy-ranked (Editor/FuzzyMatch.h) list of mentionCandidates_ against
    // mentionQuery_, capped at a handful of rows with a "N more" tail line
    // beyond that, the current selection marked "> ".
    [[nodiscard]] std::vector<DisplayLine> FormatMentionPicker(int width) const;

    const Theme&             theme_;
    editor::acp::AcpManager* acpManager_ = nullptr;
    editor::MinibufferPrompt prompt_;
    std::function<void()>    onToggleRequest_;

    bool                   collapsed_ = false;
    std::function<void()> onCollapseChanged_;
    Size                   terminalSize_{.width = 0, .height = 0};

    bool  resizing_             = false;
    Point resizeAnchorGlobal_{.x = 0, .y = 0};
    int   resizeStartPercent_ = 0;

    std::optional<std::size_t> historyIndex_;
    std::string                historyDraft_;

    // ACP checkpoint/rewind follow-up: see OpenRewindPicker/FormatRewindPicker.
    bool rewindPickerOpen_ = false;

    // @-style file-mention autocomplete follow-up -- see RefreshMentionState's
    // own doc comment. mentionStartByte_ is the byte offset of '@' itself
    // within prompt_.Text(); mentionQuery_ is everything typed after it, up
    // to the cursor. mentionCandidates_ is project-relative file paths
    // (BuildProjectTree's own output shape, ProjectFindFile's precedent),
    // re-walked fresh each time the picker opens rather than kept forever,
    // so a file the agent creates mid-conversation is still mentionable.
    bool                     mentionPickerOpen_ = false;
    std::size_t              mentionStartByte_  = 0;
    std::string              mentionQuery_;
    std::vector<std::string> mentionCandidates_;
    std::size_t              mentionSelection_ = 0;
};

} // namespace ned::ui

#endif // NED_UI_ACPPANEL_H
