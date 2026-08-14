//
// A one-row Emacs-style mode line: buffer name and line:column position,
// recomputed fresh every paint() call (no external synchronization needed).
//

#ifndef NED_UI_MODELINE_H
#define NED_UI_MODELINE_H

#include <ox/ox.hpp>

#include "ActiveBuffer.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"
#include "Theme.h"

namespace ned::ui {

class ModeLine : public ox::Widget {
  public:
    // activeBuffer, mode, and theme must outlive this ModeLine. mode is the
    // same Mode main.cpp picks once at startup and never rebinds per-buffer
    // (see BufferView's own entry in CLAUDE.md) -- shown here so it's
    // visible somewhere, mode-overrides follow-up, matching Emacs' own mode
    // line convention of naming the active major mode.
    ModeLine(const ActiveBuffer& activeBuffer, const editor::Mode& mode, const Theme& theme);

    void paint(ox::Canvas c) override;

  private:
    const ActiveBuffer& activeBuffer_;
    const editor::Mode& mode_;
    const Theme&        theme_;
};

} // namespace ned::ui

#endif // NED_UI_MODELINE_H
