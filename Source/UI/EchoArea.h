//
// A one-row status/message display -- the "half" of Emacs' minibuffer this
// project has for now. See ROADMAP.md: an interactive M-x-style prompt with
// completion is a separate, larger piece of work, deferred.
//

#ifndef NED_UI_ECHOAREA_H
#define NED_UI_ECHOAREA_H

#include <ox/ox.hpp>
#include <string>

#include "Theme.h"

namespace ned::ui {

class EchoArea : public ox::Widget {
  public:
    // theme must outlive this EchoArea (same requirement as message).
    EchoArea(const std::string& message, const Theme& theme);

    void paint(ox::Canvas c) override;

  private:
    const std::string& message_;
    const Theme&        theme_;
};

} // namespace ned::ui

#endif // NED_UI_ECHOAREA_H
