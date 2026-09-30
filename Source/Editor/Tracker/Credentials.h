//
// Hands a connection's token to curl without it ever reaching an argv
// (/proc/<pid>/cmdline is world-readable): the token command's output goes
// into a 0600 curl config file that curl reads through -K and that is
// removed once curl exits. ned keeps no copy.
//

#ifndef NED_EDITOR_TRACKER_CREDENTIALS_H
#define NED_EDITOR_TRACKER_CREDENTIALS_H

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "PrivateFile.h"
#include "Provider.h"

namespace ned::editor::tracker {

// Empty when connection has what a credentialed command needs, else why not.
[[nodiscard]] std::optional<std::string> MissingCredentials(const Connection& connection);

// The token a token command printed, surrounding whitespace trimmed. Error
// text never includes the output: it may be the token, or part of it.
[[nodiscard]] std::expected<std::string, std::string> TokenFromOutput(const std::string& connectionName, const std::string& output,
                                                                      std::optional<int> exitCode);

// A curl config `user = "email:token"` line. Fails when a value can't sit
// in a quoted config string (a line break or NUL).
[[nodiscard]] std::expected<std::string, std::string> CurlUserConfig(std::string_view email, std::string_view token);

struct CredentialedCommand {
    std::vector<std::string>           argv;
    std::shared_ptr<const PrivateFile> file; // keep alive until the command exits
};

// The second half of a credentialed fetch, separate so it is testable
// without a running event loop: the token command finished with output and
// exitCode, so write connection's curl config and point argv at it.
[[nodiscard]] std::expected<CredentialedCommand, std::string> CredentialCommand(const Connection& connection, std::vector<std::string> argv,
                                                                                const std::string& tokenOutput, std::optional<int> exitCode);

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_CREDENTIALS_H
