//
// DAP round 2: the debug console (REPL) panel -- structurally mirrors
// AcpPanel.h exactly (same dockable OverlayHost overlay shape: an opaque
// title row + content rows + one input row, registered with main.cpp's
// OverlayHost, floats over BufferView without reflowing anything). Where
// AcpPanel renders AcpManager's structured transcript, this panel keeps its
// own small transcript (input echo / result / error) -- DapManager has no
// transcript concept of its own, unlike AcpManager, since a debug session
// is a request/response protocol with no persistent conversational log.
//
// Enter sends the typed expression through DapManager::Evaluate with DAP's
// default "repl" context (distinct from ShowDebugInfo's watch-expression
// fan-out, which passes "watch") -- this panel is the actual manual-
// evaluation console dap-evaluate's one-shot echo-area prompt was always a
// smaller stand-in for.
//
// Deliberate v1 cut, same as AcpPanel/TerminalPanel: no scrollback beyond
// the tail that fits on screen, no history recall, no dock-side config
// (hardcoded bottom-dock like TerminalPanel -- a REPL is naturally
// bottom-docked and nothing asked for a right-dock option).
//

#ifndef NED_UI_DEBUGCONSOLEPANEL_H
#define NED_UI_DEBUGCONSOLEPANEL_H

#include <functional>
#include <string>
#include <vector>

#include "Editor/Dap/DapManager.h"
#include "Editor/MinibufferPrompt.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class DebugConsolePanel : public Widget {
  public:
    explicit DebugConsolePanel(const Theme& theme);

    // Connect-after-construction, unset is a safe no-op -- this class's
    // usual convention. Must outlive this DebugConsolePanel.
    void SetDapManager(editor::dap::DapManager* dapManager);

    // Invoked when the panel's own [x] close button is clicked -- wired by
    // main.cpp to the same toggle lambda dap-toggle-console drives,
    // mirroring TerminalPanel/AcpPanel's own SetOnToggleRequest exactly.
    void SetOnToggleRequest(std::function<void()> onToggle);

    void Paint(Canvas canvas) override;
    bool OnEvent(const Event& event) override;

    [[nodiscard]] bool Focusable() const override {
        return true;
    }

  private:
    enum class DisplayStyle { Plain,
                              Dim,
                              Error };
    struct DisplayLine {
        std::string  text;
        DisplayStyle style;
    };

    [[nodiscard]] Brush BrushForStyle(DisplayStyle style) const;
    [[nodiscard]] bool  CloseButtonAt(Point local) const;

    const Theme&              theme_;
    editor::dap::DapManager*  dapManager_ = nullptr;
    editor::MinibufferPrompt  prompt_;
    std::vector<DisplayLine>  history_;
    std::function<void()>     onToggleRequest_;
};

} // namespace ned::ui

#endif // NED_UI_DEBUGCONSOLEPANEL_H
