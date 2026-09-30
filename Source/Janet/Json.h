//
// JSON text to Janet values, for plugins that parse a command's JSON
// output (tracker providers over `gh --json` and Jira's REST API).
//

#ifndef NED_JANET_JSON_H
#define NED_JANET_JSON_H

#include <janet.h>

#include <string_view>

namespace ned::janet {

// Objects become tables keyed by keyword, arrays become arrays, null
// becomes nil (so a null member is absent from its table). Throws
// std::runtime_error on malformed JSON.
[[nodiscard]] Janet JsonToJanet(std::string_view text);

} // namespace ned::janet

#endif // NED_JANET_JSON_H
