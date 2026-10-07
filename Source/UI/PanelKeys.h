//
// Feeds a focused panel's keys through its mini-mode's keymap
// (Editor/PanelMode.h), collecting a multi-chord sequence the way Dispatcher
// does for a buffer: a pending prefix shows as "C-c-" with its next keys in
// the which-key hint, and an unbound sequence is reported and dropped.
//

#ifndef NED_UI_PANELKEYS_H
#define NED_UI_PANELKEYS_H

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Editor/Key.h"
#include "Editor/PanelMode.h"
#include "WhichKeyHint.h"

namespace ned::ui {

class PanelKeys {
  public:
    // mode must outlive this.
    explicit PanelKeys(const editor::PanelMode& mode);

    // The command chord completes, if any.
    [[nodiscard]] std::optional<std::string> Feed(const editor::KeyChord& chord);
    void                                     Reset();

    void SetOnMessage(std::function<void(std::string)> handler);
    void SetOnHint(std::function<void(std::optional<WhichKeyHint>)> handler);

  private:
    void Hint(std::optional<WhichKeyHint> hint);

    const editor::PanelMode&                         mode_;
    std::vector<editor::KeyChord>                    pending_;
    bool                                             hinting_ = false;
    std::function<void(std::string)>                 onMessage_;
    std::function<void(std::optional<WhichKeyHint>)> onHint_;
};

} // namespace ned::ui

#endif // NED_UI_PANELKEYS_H
