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

#include <functional>
#include <optional>
#include <string>
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
    struct DisplayLine {
        std::string  text;
        DisplayStyle style;
    };

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
};

} // namespace ned::ui

#endif // NED_UI_ACPPANEL_H
