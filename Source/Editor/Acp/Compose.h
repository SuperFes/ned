//
// The ACP compose buffer: an ordinary editing buffer for writing a long
// prompt, opened from the panel's composer (C-c '), whose keymap-only
// "acp-compose-mode" binds C-c C-c (send) and C-c C-k (cancel). The
// panel's callbacks are held here keyed by the buffer, not by the pane
// that opened it -- the buffer can be finished from any pane showing it.
// Closing the buffer any other way (C-x k) just forgets them; the panel's
// own composer text was never cleared, so nothing is lost.
//

#ifndef NED_EDITOR_ACP_COMPOSE_H
#define NED_EDITOR_ACP_COMPOSE_H

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ned::text {
class Buffer;
} // namespace ned::text

namespace ned::editor::acp {

inline constexpr std::string_view kComposeBufferName = "*acp compose*";
inline constexpr std::string_view kComposeModeName   = "acp-compose-mode";

struct ComposeCallbacks {
    std::function<void(std::string text)> onSend;
    std::function<void()>                 onCancel;
};

// Main thread only, like every buffer operation.
void                                          AttachCompose(const text::Buffer& buffer, ComposeCallbacks callbacks);
[[nodiscard]] std::optional<ComposeCallbacks> DetachCompose(const text::Buffer& buffer);

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_COMPOSE_H
