//
// Self-hosting-completion follow-up. A Janet-symbol-aware word-boundary rule
// for BufferView's ghost-text completion when editing a Janet-mode buffer
// (init.janet and friends) -- UI-agnostic and pure, the same convention
// DabbrevComplete.h/FuzzyMatch.h already establish.
//

#ifndef NED_EDITOR_JANETSYMBOLCOMPLETE_H
#define NED_EDITOR_JANETSYMBOLCOMPLETE_H

#include <cstddef>
#include <string_view>

namespace ned::editor {

// BufferView's own generic WordPrefixStart only treats ASCII alnum/'_' as
// word characters, so it cuts a token short at '-' or '/' -- wrong for a
// Janet binding name like "ned/register-command" or "backward-delete-char",
// every one of which uses both. Widens the same rule with exactly those two
// extra characters (the full set every ned/* binding name actually uses,
// see Janet/Environment.h's BindingNamesWithPrefix) so the whole name is
// recognized as one prefix to complete. ASCII-only like WordPrefixStart, for
// the same reason: every ned/* binding name is plain ASCII.
[[nodiscard]] std::size_t JanetSymbolPrefixStart(std::string_view content, std::size_t point);

} // namespace ned::editor

#endif // NED_EDITOR_JANETSYMBOLCOMPLETE_H
