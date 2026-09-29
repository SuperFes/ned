//
// ACP chat panel: a dockable OverlayHost widget displaying Manager's
// structured transcript (Manager::Transcript()), plus its own
// prompt-composition input row -- see Manager.h's own header comment for
// why the flat "*acp: <agent>*" output buffer stays untouched alongside
// this. Structurally mirrors TerminalPanel: registered with main.cpp's
// OverlayHost, floats over BufferView without reflowing anything, an
// opaque title row (agent name + state + [x] close) over content rows over
// one input row.
//
// Transcript formatting and the scroll model live in AcpPanel/. The
// transcript follows its tail until scrolled away from it (wheel,
// PageUp/PageDown, C-Home/C-End, M-Up/M-Down between prompts); new output
// never moves a scrolled-back view, and a "↓ N more" marker says what's
// below.
//

#ifndef NED_UI_ACPPANEL_H
#define NED_UI_ACPPANEL_H

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "AcpPanel/ChoicePicker.h"
#include "AcpPanel/ElicitationForm.h"
#include "AcpPanel/TranscriptFormat.h"
#include "AcpPanel/TranscriptScroll.h"
#include "ActiveBuffer.h"
#include "Editor/Acp/Compose.h"
#include "Editor/Acp/Manager.h"
#include "Editor/Acp/PanelConfig.h"
#include "Editor/Acp/PanelPicker.h"
#include "Editor/Clipboard.h"
#include "Editor/Injection.h"
#include "Editor/Lsp/Manager.h"
#include "Editor/MinibufferPrompt.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class AcpPanel : public Widget {
  public:
    explicit AcpPanel(const Theme& theme);

    // Connect-after-construction, unset is a safe no-op -- this class's
    // usual convention. Must outlive this AcpPanel.
    void SetAcpManager(editor::acp::Manager* acpManager);

    // ACP context auto-attach follow-up: the "@buffer"/"@selection"
    // built-in mentions (see RefreshMentionCandidates/ResolveMentionAttachments)
    // need to know which pane currently has keyboard focus, which changes
    // over time -- the same provider-callback shape TabBar/ProjectSidebar/
    // VcsPanel already take instead of a fixed ActiveBuffer&. Unset is a
    // safe no-op: RefreshMentionCandidates simply never offers "@selection"
    // (and "@buffer" resolves to nothing at send time) without one.
    void SetActiveBufferProvider(std::function<ActiveBuffer&()> provider);

    // Prose-check-the-composer follow-up: connect-after-construction, unset
    // is a safe no-op -- this class's usual convention. Wires
    // ProseChecker/kProseLanguageKey's existing diagnostics-only connection
    // (Editor/Lsp/Manager.h's CheckComposerProseText) onto the composer's
    // own text so a spelling/grammar issue underlines live, before the
    // prompt is ever sent -- see RequestProseCheckIfNeeded's own doc comment.
    void SetLspManager(editor::lsp::Manager* lspManager);

    // tabbed-bottom-dock-overlays follow-up: whether this panel is hosted as
    // one tab inside PanelDock.h's shared bottom dock (true, the default
    // dock side) rather than its own standalone OverlayHost overlay (false,
    // right-dock mode -- see ned/set-acp-panel-dock). Set once by main.cpp
    // right after construction, from whichever mode was configured at
    // startup; not re-read live the way most settings in this codebase are
    // (a deliberate, documented v1 cut -- see PanelDock.h's own header
    // comment). When true: Paint skips the title row/close/minimize
    // chrome entirely (PanelDock's shared tab strip owns all of that) and
    // content/input rows start at row 0 instead of row 1; OnEvent skips
    // the close/minimize button hit-tests, the collapsed-strip click
    // target, the title-row/left-edge resize-divider hit-test, and the
    // M-m collapse toggle (PanelDock owns close/maximize/resize, and
    // collapse has no meaning once switching tabs already gets a session
    // out of the way while keeping it alive). Defaults to false so a
    // default-constructed panel (every existing test, and the right-dock
    // path) keeps today's exact standalone behavior.
    void SetDockHosted(bool dockHosted);

    // Invoked when the panel's own [x] close button is clicked (standalone
    // mode) or on Esc with nothing else to do (either mode) -- wired by
    // main.cpp to the same toggle lambda acp-toggle-panel drives, mirroring
    // TerminalPanel::SetOnToggleRequest exactly.
    void SetOnToggleRequest(std::function<void()> onToggle);

    // Invoked when a tool call's location line is clicked; main.cpp opens
    // the file in an editor pane. `line` is 1-based.
    void SetOnOpenLocation(std::function<void(const std::filesystem::path& path, std::size_t line)> onOpenLocation);

    // Opens the review of a turn's file changes, from the review picker.
    void SetOnReviewRequest(std::function<void(std::string title, std::vector<editor::acp::TurnFile> files)> onReview);

    // Runs an agent's login command in a terminal: `argv` with `env` set,
    // under `label`; `done` is told whether it exited 0.
    using TerminalLoginFn = std::function<void(std::vector<std::string> argv, std::vector<std::pair<std::string, std::string>> env,
                                               std::string label, std::function<void(bool succeeded)> done)>;
    void SetOnTerminalLogin(TerminalLoginFn onTerminalLogin);

    // Where copied text goes: a copy-button click or the copy picker.
    void SetOnCopy(std::function<void(const std::string& text)> onCopy);

    // Runs a chord through the editor's keymap, returning whether it left a
    // key sequence waiting for more. C-c and C-x sequences the panel doesn't
    // handle itself go there (C-c c closing the panel, C-x o, ...).
    void SetOnForwardChord(std::function<bool(const editor::KeyChord& chord)> forward);

    // How a question's URL is opened; editor::link::OpenUrl when unset.
    void SetUrlOpener(std::function<bool(const std::string& url)> opener);

    // Where C-v reads the clipboard from; the system clipboard when unset.
    void SetClipboardSource(std::function<std::optional<editor::ClipboardImage>()> image,
                            std::function<std::optional<std::string>()>            text);

    // C-c ' hands the composer's text to this (main.cpp: WindowManager::
    // RequestAcpCompose) to continue in a full editing buffer; sending from
    // there submits it through the composer, cancelling refocuses the panel
    // with its text untouched.
    void SetOnComposeRequest(std::function<void(std::string seed, editor::acp::ComposeCallbacks callbacks)> onCompose);
    // Invoked after the compose buffer is finished either way, to bring the
    // panel back -- main.cpp's show-and-focus.
    void SetOnRefocusRequest(std::function<void()> onRefocus);

    // Manager::SetOnAttention's handler (main.cpp wires it). Unless the
    // panel has focus, marks the title with "● " until it does; and raises
    // a desktop notification through the notifier when the panel isn't
    // focused or the turn ran long enough that the user has likely looked
    // away (kLongTurn).
    void NoteAttention(editor::acp::Manager::Attention attention, std::chrono::steady_clock::duration turnElapsed);
    // What NoteAttention notifies through; main.cpp passes
    // acp::SendDesktopNotification. Unset is a safe no-op.
    void SetDesktopNotifier(std::function<void(const std::string& title, const std::string& body)> notifier);

    // This tab's dynamic label for PanelDock's shared tab strip (dock-hosted
    // mode only, but harmless to call either way): "<agent name> [<state>]",
    // the exact text this panel's own title row draws in standalone mode.
    [[nodiscard]] std::string TitleText() const;

    // Replaces the transcript view with one of the pickers (see
    // acppanel::ChoicePicker): past turns to rewind to, the session's modes,
    // its models, its config options, or earlier sessions to resume.
    // main.cpp's SetOnAcpPickerRequest wiring calls this after
    // showing/focusing the panel. A no-op if no Manager is set.
    void OpenPicker(editor::acp::PanelPicker picker);

    // Flips ned/set-acp-follow-agent, confirming on the status row.
    void ToggleFollowAgent();

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
    // PanelSizePercent() delta (this panel's own Box only ever reports its
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
    using DisplayStyle = acppanel::DisplayStyle;
    using DisplayLine  = acppanel::DisplayLine;
    using InlineSpan   = acppanel::InlineSpan;

    // PaintUtf8Row's multi-segment sibling: paints `text` in `baseBrush`
    // except where `spans` restyle it (bold, or a documentHighlightBackground
    // tint for inline code).
    void PaintStyledRow(Canvas& canvas, int x, int y, std::string_view text, const std::vector<InlineSpan>& spans, const Brush& baseBrush,
                        int maxColumns) const;

    // Drops a session list still loading, telling the agent to stop.
    void StopAwaitingSessions();
    void StartLogin(const editor::acp::Manager::AuthMethod& method);
    // A picker over one config option's values; nullptr opens one saying
    // the agent offers no such setting.
    [[nodiscard]] acppanel::ChoicePicker ConfigValuePicker(const editor::acp::Manager::ConfigOption* option,
                                                           std::string_view                          missing);
    // The row between transcript and composer: mode/model/effort on the
    // left, the in-flight timer, context use and cost on the right. Empty
    // when there's nothing to show, which gives the row back.
    [[nodiscard]] std::pair<std::string, std::string> StatusLine() const;
    // The wrapped transcript rows, rebuilt only when the transcript, the
    // pending permission prompt or the width changed since the last frame.
    [[nodiscard]] const std::vector<acppanel::PhysicalLine>& TranscriptRows(int width);
    // Rows the transcript area gets this frame -- what PageUp/PageDown and
    // the scroll keys measure against between frames.
    [[nodiscard]] int TranscriptViewportRows() const;
    // Whether transcript entry `index` currently shows its details -- see
    // TranscriptFormatOptions::expanded.
    [[nodiscard]] bool EntryExpanded(std::size_t index) const;
    // Runs the LineAction of the transcript row painted at panel-local row y.
    // Returns false when that row has none.
    bool ActivateRowAt(int y);
    // Scrolls to the previous (direction < 0) or next UserMessage entry
    // relative to the current top row.
    void                                   JumpToPrompt(int direction);
    [[nodiscard]] Brush                    BrushForStyle(DisplayStyle style) const;
    [[nodiscard]] Brush                              SyntaxBrush(editor::SyntaxClass cls, editor::CaptureId captureId) const;
    [[nodiscard]] std::vector<editor::HighlightSpan> HighlightCode(std::string_view language, std::string_view code);
    void                                             Copy(const std::string& text);
    void                                             ShowNotice(std::string notice);
    void                                             PasteFromClipboard();
    // Keeps form_ in step with the Manager's pending question.
    void                                             SyncElicitation();
    void                                             SubmitElicitation();
    [[nodiscard]] std::string                        PendingImagesLine() const;
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
    // End -- unlike ProjectSidebar's width_, PanelSizePercent() already
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
    // place of the sigil ("@" or "/") + the typed query, replacing
    // [mentionStartByte_, cursorByteOffset) with "@<path> " or "/<name> ",
    // and returns the candidate accepted -- MinibufferPrompt::SetText's own
    // documented "cursor moves to the end of the replacement" behavior
    // applies here too (same as Tab-completion elsewhere in this codebase),
    // so a mention accepted with trailing text already typed after the
    // cursor loses that trailing text's own cursor position, a deliberate,
    // pre-existing SetText limitation, not a new one.
    std::optional<std::string>                                  AcceptMentionCandidate();
    [[nodiscard]] const editor::acp::Manager::AvailableCommand* FindCommand(std::string_view name) const;
    // Sends the composer's text (mentions resolved into attachments) and
    // clears it -- queued instead while a turn is running, or with `steer`,
    // added to the running turn (Manager::Steer). A no-op when it's empty.
    void SubmitComposer(bool steer = false);
    // Moves the composer's cursor one row up (direction < 0) or down, as
    // laid out at the last painted width. False when already on the
    // first/last row.
    bool MoveComposerVertically(int direction);
    // mentionPickerOpen_'s own content: a
    // fuzzy-ranked (Editor/FuzzyMatch.h) list of mentionCandidates_ against
    // mentionQuery_, capped at a handful of rows with a "N more" tail line
    // beyond that, the current selection marked "> ".
    [[nodiscard]] std::vector<DisplayLine> FormatMentionPicker() const;

    // ACP context auto-attach follow-up: called once, right before sending,
    // on the composer's own about-to-be-sent text. Finds the built-in
    // "@buffer"/"@selection" tokens RefreshMentionCandidates/
    // AcceptMentionCandidate already let the user splice in (word-boundary
    // matched, the same "@ must start a word" rule RefreshMentionState
    // uses), replaces each one found with a short "[attached: name]" marker
    // in `text`, and returns one Manager::PromptAttachment per token
    // actually resolved (a token present with no activeBufferProvider_ set,
    // or "@selection" with no active mark, is left as literal text --
    // silently not treated as a mention at all, rather than erroring).
    [[nodiscard]] std::vector<editor::acp::Manager::PromptAttachment> ResolveMentionAttachments(std::string& text) const;
    // Every "@path" word in `text` naming an existing file (relative to the
    // project root, or absolute) as a resource_link attachment. The text is
    // left as typed: the agent sees the mention and gets the link.
    static void AppendFileMentionLinks(const std::string& text, std::vector<editor::acp::Manager::PromptAttachment>& attachments);

    // Prose-check-the-composer follow-up: called once per Paint() (there's
    // no per-keystroke edit hook the way RefreshMentionState has -- Paint()
    // already runs every frame regardless of what triggered the redraw, so
    // comparing against lastProseCheckedText_ here catches every composer
    // mutation without needing one at each of InsertChar/DeleteBackward/
    // DeleteForward/HistoryPrevious/HistoryNext/AcceptMentionCandidate's own
    // call sites). A no-op once the text is unchanged since the last call --
    // Manager::CheckComposerProseText's own debounce covers the rest.
    void RequestProseCheckIfNeeded();

    const Theme&             theme_;
    editor::acp::Manager* acpManager_ = nullptr;
    editor::MinibufferPrompt prompt_;
    std::function<void()>    onToggleRequest_;
    bool                     dockHosted_ = false; // see SetDockHosted

    // ACP context auto-attach follow-up -- see SetActiveBufferProvider's
    // own doc comment.
    std::function<ActiveBuffer&()> activeBufferProvider_;

    // Prose-check-the-composer follow-up -- see SetLspManager/
    // RequestProseCheckIfNeeded's own doc comments. composerProseDiagnostics_
    // holds byte ranges into prompt_.Text() *as of whichever check last
    // returned* -- slightly stale relative to what's typed since (the same
    // lag every real buffer's own diagnostics have against live typing),
    // clamped defensively at render time rather than assumed still in range.
    editor::lsp::Manager*              lspManager_ = nullptr;
    std::string                           lastProseCheckedText_;
    std::vector<text::Buffer::Diagnostic> composerProseDiagnostics_;

    bool                  collapsed_ = false;
    std::function<void()> onCollapseChanged_;
    Size                  terminalSize_{.width = 0, .height = 0};

    bool  resizing_ = false;
    Point resizeAnchorGlobal_{.x = 0, .y = 0};
    int   resizeStartPercent_ = 0;

    // Double-click detection for the resize divider, ProjectSidebar/VcsPanel's
    // own precedent: a second press within kDoubleClickWindow collapses
    // instead of starting a resize; a real drag clears the pending state
    // (see UpdateResize). Standalone mode only, alongside every other
    // divider-hit-test this panel does -- see SetDockHosted's doc comment.
    bool                                  dividerClickPending_ = false;
    std::chrono::steady_clock::time_point lastDividerPressTime_;

    // The transcript area's scroll position, and the wrapped rows it
    // indexes into (see TranscriptRows).
    acppanel::TranscriptScroll          scroll_;
    std::vector<acppanel::DisplayLine>  transcriptLines_;
    std::vector<acppanel::PhysicalLine> transcriptRows_;
    std::size_t                         transcriptRowsGeneration_ = static_cast<std::size_t>(-1);
    int                                 transcriptRowsWidth_      = -1;
    bool                                transcriptRowsPending_    = false;
    int                                 lastViewportRows_         = 0;
    int                                 lastComposerWidth_        = 0; // what Up/Down lay the composer out at
    int                                 lastFirstRow_             = 0;
    int                                 lastTitleRows_            = 0;
    bool                                lastShowedTranscript_     = false;

    // Expansion: entries in toggledEntries_ show the opposite of their
    // default, and C-o's verbose_ flips every default to expanded. Both feed
    // the row cache through viewGeneration_ and the two settings it samples.
    std::set<std::size_t>        toggledEntries_;
    bool                         verbose_                      = false;
    std::size_t                  viewGeneration_               = 0;
    std::size_t                  transcriptRowsViewGeneration_ = static_cast<std::size_t>(-1);
    editor::acp::ToolCallDisplay transcriptRowsToolCalls_      = editor::acp::ToolCallDisplay::Collapsed;
    editor::acp::ThinkingDisplay transcriptRowsThinking_       = editor::acp::ThinkingDisplay::Collapsed;
    std::size_t                  lastTranscriptSize_           = 0;
    long long                    transcriptRowsTick_           = -1;

    std::function<void(const std::filesystem::path&, std::size_t)>  onOpenLocation_;
    std::function<void(const std::string&, const std::string&)>     desktopNotifier_;
    std::function<void(std::string, editor::acp::ComposeCallbacks)> onComposeRequest_;
    std::function<void()>                                           onRefocusRequest_;
    std::function<void(const std::string&)>                         onCopy_;
    std::function<void(std::string, std::vector<editor::acp::TurnFile>)> onReviewRequest_;
    TerminalLoginFn                                                      onTerminalLogin_;
    std::function<std::optional<editor::ClipboardImage>()>          clipboardImage_;
    std::function<std::optional<std::string>()>                     clipboardText_;
    std::function<bool(const std::string&)>                              urlOpener_;

    // The agent's open question, as a form owning the keyboard.
    std::optional<acppanel::ElicitationForm> form_;
    std::size_t                              formElicitationId_ = 0;

    // Images pasted into the composer, sent with the next prompt.
    std::vector<editor::acp::Manager::PromptAttachment> pendingImages_;
    std::size_t                                         imagesPasted_ = 0;

    // A short confirmation on the status row ("copied 3 lines"), until it expires.
    std::string                           notice_;
    std::chrono::steady_clock::time_point noticeUntil_;

    // Fenced-code highlighting: each fence tag's highlighter, and each
    // block's spans, so re-formatting a streaming transcript re-highlights
    // only the block still growing.
    editor::EmbeddedLanguageCache                                       codeLanguages_;
    std::unordered_map<std::string, std::vector<editor::HighlightSpan>> codeHighlights_;
    mutable std::unordered_map<std::uint32_t, Brush>                    syntaxBrushes_;
    mutable std::size_t                                                 syntaxBrushesGeneration_ = static_cast<std::size_t>(-1);
    mutable std::string                                                 syntaxBrushesTheme_;
    // C-c was pressed: the next chord completes a C-c sequence.
    bool                                         controlCPending_ = false;
    editor::KeyChord                             controlCChord_{};
    std::function<bool(const editor::KeyChord&)> forwardChord_;
    bool                                         forwardingSequence_ = false; // the rest of a forwarded sequence goes too
    bool attention_       = false; // see NoteAttention

    std::optional<std::size_t> historyIndex_;
    std::string                historyDraft_;

    // See OpenPicker. pendingPicker_ is a picker a choice opened (an
    // option's value list, after picking the option), installed once the
    // choosing picker has closed.
    std::optional<acppanel::ChoicePicker> picker_;
    std::optional<acppanel::ChoicePicker> pendingPicker_;
    // The session picker fills in asynchronously; its answer is dropped
    // unless the "Loading" picker it replaces is still the one open.
    bool        awaitingSessions_ = false;
    std::size_t sessionsRequest_  = 0;

    // @-style file-mention autocomplete follow-up -- see RefreshMentionState's
    // own doc comment. mentionStartByte_ is the byte offset of '@' itself
    // within prompt_.Text(); mentionQuery_ is everything typed after it, up
    // to the cursor. mentionCandidates_ is project-relative file paths
    // (BuildProjectTree's own output shape, ProjectFindFile's precedent),
    // re-walked fresh each time the picker opens rather than kept forever,
    // so a file the agent creates mid-conversation is still mentionable.
    // What the open completion list completes: "@" file mentions, or "/"
    // commands from the agent's available_commands_update.
    enum class CompletionKind { File,
                                Command };
    CompletionKind           completionKind_    = CompletionKind::File;
    bool                     mentionPickerOpen_ = false;
    std::size_t              mentionStartByte_  = 0;
    std::string              mentionQuery_;
    std::vector<std::string> mentionCandidates_;
    std::size_t              mentionSelection_ = 0;
};

} // namespace ned::ui

#endif // NED_UI_ACPPANEL_H
