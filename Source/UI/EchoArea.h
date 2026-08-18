//
// A one-row status/message display -- the "half" of Emacs' minibuffer this
// project has for now. See ROADMAP.md: an interactive M-x-style prompt with
// completion is a separate, larger piece of work, deferred.
//

#ifndef NED_UI_ECHOAREA_H
#define NED_UI_ECHOAREA_H

#include <string>
#include <string_view>

#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

// fuzzy-candidate-list-styling follow-up: EchoArea's message is one plain
// string with one uniform Theme::echoArea brush across the whole row -- but
// a fuzzy-narrowed candidate list (M-x, project-find-file) needs the
// selected entry visually distinct from the rest, and there's no separate
// widget to render it in (see BufferView.h's own note on why: no floating/
// popup concept in this codebase). Rather than turning statusMessage_ into a
// rich-text type -- which every other writer of it (isearch, save-buffer,
// error messages, ...) would then have to either participate in or ignore --
// these wrap a substring in a closed, private pair of C0 control-byte
// sentinels (never legitimately present in any status message: paths,
// command names, regex patterns, prose) that EchoArea::Paint recognizes,
// strips before display, and applies as a per-cell style override for the
// span between them. Every other caller of statusMessage_ keeps writing
// plain text and is completely unaffected. EmphasizeForEchoArea marks the
// selected candidate (bold); DimForEchoArea marks the rest (foreground
// blended halfway toward the background, the same
// ftxui::Color::Interpolate mechanism ModeLine's gradient already uses).
[[nodiscard]] std::string EmphasizeForEchoArea(std::string_view text);
[[nodiscard]] std::string DimForEchoArea(std::string_view text);

class EchoArea : public Widget {
  public:
    // theme must outlive this EchoArea (same requirement as message).
    EchoArea(const std::string& message, const Theme& theme);

    void Paint(Canvas c) override;

  private:
    const std::string& message_;
    const Theme&       theme_;
};

} // namespace ned::ui

#endif // NED_UI_ECHOAREA_H
