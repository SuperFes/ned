//
// ned --protocol-dump: a captured host-protocol byte stream
// (Docs/ClientServerProtocol.md, section 2) -- one direction of a
// connection, as a tee between the editor and ssh records it -- printed as
// one JSON message per line. A byte string prints as text when it's UTF-8,
// else as its length and its leading bytes in hex. What an agent session
// prints ahead of its start marker is skipped.
//

#ifndef NED_EDITOR_REMOTE_PROTOCOLDUMP_H
#define NED_EDITOR_REMOTE_PROTOCOLDUMP_H

#include <ostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ned::editor::remote {

// value with each byte string made readable.
[[nodiscard]] nlohmann::json ReadableMessage(const nlohmann::json& value);

// Prints each message in capture. A frame that won't decode prints as an
// error line and the dump goes on; a malformed or truncated stream ends it.
// False if anything didn't decode.
bool DumpProtocol(std::string capture, std::ostream& out, std::ostream& err);

// Dumps each path, or stdin for none or "-". The process's exit status.
[[nodiscard]] int RunProtocolDump(const std::vector<std::string>& paths, std::ostream& out, std::ostream& err);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_PROTOCOLDUMP_H
