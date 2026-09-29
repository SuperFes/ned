//
// The ACP panel's session strip: a row of tabs, one per conversation on the
// agent's connection, with a close button on the current one and a "+" for
// a new conversation. Laid out here, painted and hit-tested by AcpPanel.
//

#ifndef NED_UI_ACPPANEL_SESSIONSTRIP_H
#define NED_UI_ACPPANEL_SESSIONSTRIP_H

#include <cstdint>
#include <string>
#include <vector>

#include "Editor/Acp/Manager.h"

namespace ned::ui::acppanel {

struct SessionStripItem {
    // More stands for tabs scrolled out of view; clicking it picks the
    // nearest hidden one.
    enum class Kind { Tab,
                      Close,
                      New,
                      More };
    Kind          kind    = Kind::Tab;
    std::uint64_t key     = 0; // the session a Tab, Close or More acts on
    int           x       = 0;
    int           columns = 0;
    std::string   text;
    bool          current   = false;
    bool          live      = true;
    bool          attention = false;
};

// Tabs numbered from 1 in order, each "n label"; a label shrinks, down to
// nothing, before tabs are scrolled away, and the current tab always shows.
[[nodiscard]] std::vector<SessionStripItem> LayoutSessionStrip(const std::vector<editor::acp::Manager::SessionTab>& tabs, int width);

// The item at column x, if any.
[[nodiscard]] const SessionStripItem* SessionStripItemAt(const std::vector<SessionStripItem>& items, int x);

} // namespace ned::ui::acppanel

#endif // NED_UI_ACPPANEL_SESSIONSTRIP_H
