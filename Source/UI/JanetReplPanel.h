//
// REPL-engine follow-up: the in-process Janet REPL panel. Where
// DebugConsolePanel evaluates DAP expressions against a running debug
// session, this panel evaluates Janet expressions directly against the
// live, already-running janet::Environment (SetEnv) -- a real "live Lisp
// image" REPL that can see editor state (open buffers, registered
// commands, Janet-defined variables, ...), not a fresh subprocess. This is
// the "built-in" half of the REPL engine; PHP/Python/Perl/... REPLs are the
// other half -- see UI/TerminalPanel.h's argv/label generalization and
// Editor/Repl/ReplConfig.h, which spawn the language's own real interactive
// CLI REPL on a pty instead, since those have no in-process equivalent to
// evaluate against.
//
// Structurally this mirrors DebugConsolePanel.h almost exactly (transcript
// + input row + history + scrollback + search, hosted as one PanelDock tab)
// -- see that file's own header comment for the shape this reuses
// unmodified. It differs only where evaluation itself must:
//
// - SetEnv(JanetTable*) replaces SetDapManager -- connect-after-construction,
//   nullable, same convention.
// - Enter calls ned::janet::DoStringCapturingStacktrace directly (the same
//   primitive ned/register-command's own invocation path already uses) --
//   synchronous, not a callback, since in-process eval can't fail
//   asynchronously the way a DAP round-trip can. Success formats the
//   result via Janet's own janet_to_string (what the janet CLI REPL itself
//   prints); failure shows the captured stacktrace text.
// - No CommandContextScope is opened around the eval: ScriptingSessionScope
//   is already active for the whole process (main.cpp), so any ned/*
//   binding that only needs the registry/keymap works from here. A binding
//   that specifically requires an *active command invocation's*
//   CommandContext (rare -- most ned/* bindings take explicit arguments)
//   won't work from the REPL; a deliberate v1 cut, not a bug.
//

#ifndef NED_UI_JANETREPLPANEL_H
#define NED_UI_JANETREPLPANEL_H

#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Key.h"
#include "Editor/LineListSearch.h"
#include "Editor/MinibufferPrompt.h"
#include "Editor/PromptHistory.h"
#include "Theme.h"
#include "Widget.h"

struct JanetTable; // <janet.h>'s own typedef target -- kept as a forward
                    // declaration here so this UI header doesn't pull in
                    // the full Janet C API; JanetReplPanel.cpp includes
                    // <janet.h> for real.

namespace ned::ui {

class JanetReplPanel : public Widget {
  public:
    explicit JanetReplPanel(const Theme& theme);

    // Connect-after-construction, nullable, this class's usual convention.
    // Must outlive this JanetReplPanel. Unset: Enter reports "No Janet
    // environment available." instead of evaluating.
    void SetEnv(JanetTable* env);

    // Same convention as DebugConsolePanel::SetPromptHistory -- the shared
    // process-wide editor::PromptHistory instance, under the key
    // "janet-repl".
    void SetPromptHistory(editor::PromptHistory* promptHistory);

    // Invoked on Esc (with no search active) -- wired by main.cpp to the
    // toggle-janet-repl command's toggle lambda, mirroring
    // DebugConsolePanel/TerminalPanel's own SetOnToggleRequest exactly.
    void SetOnToggleRequest(std::function<void()> onToggle);

    void Paint(Canvas canvas) override;
    bool OnEvent(const Event& event) override;

    [[nodiscard]] bool Focusable() const override {
        return true;
    }

    // This tab's dynamic label for PanelDock's shared tab strip: "Janet
    // REPL" plus whichever of search-status/scrollback applies --
    // DebugConsolePanel::TitleText's exact shape.
    [[nodiscard]] std::string TitleText() const;

    // Testing seam: drives one eval the same way Enter does, without going
    // through key events -- TerminalPanel::Feed's "public test seam"
    // convention.
    void EvaluateForTesting(std::string_view code);

  private:
    enum class DisplayStyle { Plain,
                              Dim,
                              Error };
    struct DisplayLine {
        std::string  text;
        DisplayStyle style;
    };

    [[nodiscard]] Brush BrushForStyle(DisplayStyle style) const;
    [[nodiscard]] int   ContentRows() const;
    void                ScrollBy(int deltaLines);
    void                ScrollToShowIndex(std::size_t index);
    bool                TryNavigateHistory(const editor::KeyChord& chord);
    bool                HandleSearchKey(const editor::KeyChord& chord);

    // The actual eval -- Enter and EvaluateForTesting both funnel through
    // this. Appends the echoed input plus the result/error to history_.
    void Evaluate(const std::string& code);

    const Theme&             theme_;
    JanetTable*              env_           = nullptr;
    editor::PromptHistory*   promptHistory_ = nullptr;
    editor::MinibufferPrompt prompt_;
    std::vector<DisplayLine> history_;
    std::function<void()>    onToggleRequest_;
    int                      scrollbackOffset_ = 0;

    std::vector<std::string>              searchLines_;
    std::optional<editor::LineListSearch> search_;
    int                                   searchOriginalScrollback_ = 0;

    static constexpr std::size_t kNoHistoryIndex = std::numeric_limits<std::size_t>::max();
    std::size_t                  historyIndex_   = kNoHistoryIndex;
    std::string                  historyStash_;
};

} // namespace ned::ui

#endif // NED_UI_JANETREPLPANEL_H
