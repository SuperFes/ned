//
// Desktop notifications for the ACP panel: a turn finished, or the agent
// is waiting on a permission decision, while the user was looking
// elsewhere. Sent by running a configured command (ned/set-acp-notify-
// command) with the title and body appended as two more arguments --
// notify-send's own shape, the default.
//

#ifndef NED_EDITOR_ACP_NOTIFY_H
#define NED_EDITOR_ACP_NOTIFY_H

#include <string>
#include <vector>

namespace ned::editor::acp {

// Defaults to {"notify-send", "-a", "ned"}; an empty argv turns
// notifications off.
void                                   SetAcpNotifyCommand(std::vector<std::string> argv);
[[nodiscard]] std::vector<std::string> AcpNotifyCommand();

// The argv SendDesktopNotification runs; empty when notifications are off.
[[nodiscard]] std::vector<std::string> NotifyArgv(const std::string& title, const std::string& body);

// Runs NotifyArgv without a shell and without waiting on it (the child is
// reaped by a detached thread, OpenUrl's own shape). False when
// notifications are off or fork fails.
bool SendDesktopNotification(const std::string& title, const std::string& body);

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_NOTIFY_H
