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

    [[nodiscard]] std::vector<DisplayLine> FormatTranscript() const;
    [[nodiscard]] Brush                    BrushForStyle(DisplayStyle style) const;
    [[nodiscard]] bool                     CloseButtonAt(Point local) const;

    const Theme&             theme_;
    editor::acp::AcpManager* acpManager_ = nullptr;
    editor::MinibufferPrompt prompt_;
    std::function<void()>    onToggleRequest_;
};

} // namespace ned::ui

#endif // NED_UI_ACPPANEL_H
