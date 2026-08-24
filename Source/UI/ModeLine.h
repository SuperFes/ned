//
// A one-row Emacs-style mode line: buffer name and line:column position,
// recomputed fresh every paint() call. The one exception is the LSP
// activity/status entry's minimum-visible-duration hold (see Paint's own
// comment) -- everything else needs no external synchronization.
//

#ifndef NED_UI_MODELINE_H
#define NED_UI_MODELINE_H

#include <chrono>
#include <functional>
#include <vector>

#include "ActiveBuffer.h"
#include "Editor/BackgroundActivity.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Mode.h"
#include "Editor/Org.h"
#include "Text/Buffer.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class ModeLine : public Widget {
  public:
    // activeBuffer, mode, and theme must outlive this ModeLine. mode is the
    // same Mode main.cpp picks once at startup and never rebinds per-buffer
    // (see BufferView's own entry in CLAUDE.md) -- shown here so it's
    // visible somewhere, mode-overrides follow-up, matching Emacs' own mode
    // line convention of naming the active major mode.
    ModeLine(const ActiveBuffer& activeBuffer, const editor::Mode& mode, const Theme& theme);

    void Paint(Canvas c) override;

    // Chrome-redesign follow-up: when set and returning true, the gradient
    // uses the theme's modeLineFocusedGradientStart/End (the accent-tinted
    // pair) instead of the plain one -- the focus signal for which pane has
    // the keyboard. Unset (the default, every pre-existing construction
    // site and test) means never focused, i.e. the plain gradient. Pane's
    // ctor wires this to its own BufferView's Widget::Focused().
    void SetFocusProvider(std::function<bool()> provider);

    // mode-line-lsp-indicator follow-up: unset (the default, every
    // pre-existing construction site and test) means never show an LSP
    // indicator -- same "safe no-op until wired" convention as
    // SetFocusProvider above and every other Set* hook in this codebase.
    // Wired by Pane/WindowManager::SetLspManager alongside BufferView's own
    // copy of the same pointer.
    void SetLspManager(editor::lsp::LspManager* lspManager);

  private:
    const ActiveBuffer&      activeBuffer_;
    const editor::Mode&      mode_;
    const Theme&             theme_;
    std::function<bool()>    focusProvider_;
    editor::lsp::LspManager* lspManager_ = nullptr;

    // minimum-visible-duration follow-up: the last non-empty
    // ActiveBackgroundActivities() snapshot, held and re-shown for
    // kMinimumVisibleDuration after the real activity list goes empty. A
    // sub-frame-length round trip (a fast hover/completion response, in
    // particular) could otherwise begin and end within a single Paint()
    // call, blinking the "LSP" spinner+detail on and off quickly enough to
    // read as a rendering glitch rather than a real, if brief, event -- a
    // real, reported live-use complaint, not a hypothetical. Deliberately
    // local to this widget's own rendering rather than a change to
    // BackgroundActivity itself, which keeps its existing immediate-
    // empty-on-End semantics for every other consumer (a shared, heavily
    // depended-on process-wide primitive with its own test suite -- adding
    // hold-over there leaked a real 300ms grace window across unrelated,
    // fast-running unit tests sharing that one static registry).
    std::vector<editor::BackgroundActivity> lastShownActivities_;
    std::chrono::steady_clock::time_point   lastShownActivitiesAt_{};
};

} // namespace ned::ui

#endif // NED_UI_MODELINE_H
