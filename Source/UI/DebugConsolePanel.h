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
// DAP round 4: scrollback (mirrors TerminalPanel.h's own scrollbackOffset_
// mechanism exactly -- WheelUp/Down, Shift-PageUp/Down, a "(scrollback)"
// title suffix, and TerminalPanel's own documented rule that new output
// never yanks a scrolled-back view back to live; only submitting a new
// expression does) and input history-recall (M-p/M-n over the same
// Editor/PromptHistory.h ring BufferView's own prompts use, under the key
// "debug-console") close the v1 cuts this comment used to describe.
//
// DAP round 5 (debug-console-search): C-s/C-r search over history_, using
// the new Editor/LineListSearch.h primitive -- IncrementalSearch.h's shape
// adapted to a plain line list instead of a text::Buffer, since this
// panel's transcript has no buffer/undo semantics to hang a real isearch
// off of. Same Emacs isearch key conventions BufferView::HandleSearchKey
// uses (C-s/C-r repeat-or-reverse depending on current direction, Enter
// accepts, Esc cancels back to the pre-search scrollbackOffset_).
//
// Deliberate v1 cut, same as AcpPanel/TerminalPanel: no dock-side config
// (hardcoded bottom-dock like TerminalPanel -- a REPL is naturally
// bottom-docked and nothing asked for a right-dock option).
//

#ifndef NED_UI_DEBUGCONSOLEPANEL_H
#define NED_UI_DEBUGCONSOLEPANEL_H

#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "Editor/Dap/DapManager.h"
#include "Editor/Key.h"
#include "Editor/LineListSearch.h"
#include "Editor/MinibufferPrompt.h"
#include "Editor/PromptHistory.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class DebugConsolePanel : public Widget {
  public:
    explicit DebugConsolePanel(const Theme& theme);

    // Connect-after-construction, unset is a safe no-op -- this class's
    // usual convention. Must outlive this DebugConsolePanel.
    void SetDapManager(editor::dap::DapManager* dapManager);

    // DAP round 4: same Set*-after-construction, nullable-pointer convention
    // as SetDapManager -- main.cpp passes the same process-wide
    // editor::PromptHistory instance BufferView already uses, under the
    // "debug-console" key. Unset means M-p/M-n are simply not handled here
    // (never crashes, just no recall).
    void SetPromptHistory(editor::PromptHistory* promptHistory);

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
    // DAP round 4: this panel's own content-row formula (height - 2, title +
    // input rows) -- shared by Paint's render loop and ScrollBy's clamp
    // bound, TerminalPanel::ContentRows's own precedent.
    [[nodiscard]] int ContentRows() const;
    void              ScrollBy(int deltaLines);
    // Moves scrollbackOffset_ so history_[index] lands as the bottom-most
    // visible content row -- HandleSearchKey's own "jump to the match"
    // primitive, same clamp bound ScrollBy uses.
    void ScrollToShowIndex(std::size_t index);
    // BufferView::TryNavigatePromptHistory's exact logic, adapted to this
    // panel's plain (non-optional) prompt_ -- returns true (chord consumed)
    // whenever it's an M-p/M-n chord, false otherwise.
    bool TryNavigateHistory(const editor::KeyChord& chord);
    // BufferView::HandleSearchKey's exact shape, adapted to search_ over
    // this panel's own transcript -- only called while search_ already has
    // a value. Always returns true (every key is consumed mid-search).
    bool HandleSearchKey(const editor::KeyChord& chord);

    const Theme&              theme_;
    editor::dap::DapManager*  dapManager_    = nullptr;
    editor::PromptHistory*    promptHistory_ = nullptr;
    editor::MinibufferPrompt  prompt_;
    std::vector<DisplayLine>  history_;
    std::function<void()>     onToggleRequest_;
    int                       scrollbackOffset_ = 0;
    // debug-console-search: a snapshot of history_'s text, taken when a
    // search session starts -- IncrementalSearch's own "materialize once"
    // precedent, adapted since this transcript can keep growing (a pending
    // evaluate response) while a search is active; new output mid-search
    // simply isn't searched until the next session. Declared before search_
    // so it outlives the LineListSearch holding a reference into it.
    std::vector<std::string>              searchLines_;
    std::optional<editor::LineListSearch> search_;
    int                                   searchOriginalScrollback_ = 0;
    // TryNavigateHistory's own browsing cursor -- kNoHistoryIndex means
    // "live editing," mirroring BufferView::promptHistoryIndex_/kNoHistoryIndex
    // exactly (own copy, not shared: this panel doesn't have access to
    // BufferView's private enum-scoped constant).
    static constexpr std::size_t kNoHistoryIndex = std::numeric_limits<std::size_t>::max();
    std::size_t                  historyIndex_   = kNoHistoryIndex;
    std::string                  historyStash_;
};

} // namespace ned::ui

#endif // NED_UI_DEBUGCONSOLEPANEL_H
