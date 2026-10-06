//
// ned-agent's whole life: a Dispatcher answering the protocol over one fd
// pair until the editor hangs up.
//

#ifndef NED_EDITOR_HOST_AGENT_H
#define NED_EDITOR_HOST_AGENT_H

#include <optional>
#include <string>

namespace ned::editor::host {

// Takes ownership of both fds. Returns once the editor hangs up, with the
// reason when a protocol error ended the session instead.
[[nodiscard]] std::optional<std::string> RunAgent(int readFd, int writeFd, std::string build);

} // namespace ned::editor::host

#endif // NED_EDITOR_HOST_AGENT_H
