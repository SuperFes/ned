//
// JSON text to and from Janet values, for plugins that parse a command's
// JSON output or send it JSON (tracker providers over `gh --json` and
// Jira's REST API).
//

#ifndef NED_JANET_JSON_H
#define NED_JANET_JSON_H

#include <janet.h>

#include <string>
#include <string_view>

namespace ned::janet {

// Objects become tables keyed by keyword, arrays become arrays, null
// becomes nil (so a null member is absent from its table). Throws
// std::runtime_error on malformed JSON.
[[nodiscard]] Janet JsonToJanet(std::string_view text);

// The reverse: tables and structs become objects (keys must be keywords,
// strings or symbols), arrays and tuples arrays, nil null. A whole number
// is written without a fraction. Throws std::runtime_error on anything
// else (a function, a buffer...).
[[nodiscard]] std::string JanetToJson(Janet value);

} // namespace ned::janet

#endif // NED_JANET_JSON_H
