// Range tables for the derived identifier properties and the emoji
// properties -- see Tools/gen-unicode-tables.py, which generates
// UnicodeTables.cpp. Each table is a flat list of half-open [start, end)
// pairs, ascending and disjoint.

#ifndef NED_EDITOR_GRAMMAR_COMPILE_UNICODETABLES_H
#define NED_EDITOR_GRAMMAR_COMPILE_UNICODETABLES_H

#include <cstddef>
#include <cstdint>

namespace ned::editor::grammar::compile::unicode {

extern const std::uint32_t kXidStart[];
extern const std::size_t   kXidStartCount;
extern const std::uint32_t kXidContinue[];
extern const std::size_t   kXidContinueCount;
extern const std::uint32_t kIdStart[];
extern const std::size_t   kIdStartCount;
extern const std::uint32_t kIdContinue[];
extern const std::size_t   kIdContinueCount;
extern const std::uint32_t kEmoji[];
extern const std::size_t   kEmojiCount;
extern const std::uint32_t kEmojiPresentation[];
extern const std::size_t   kEmojiPresentationCount;
extern const std::uint32_t kEmojiModifier[];
extern const std::size_t   kEmojiModifierCount;
extern const std::uint32_t kEmojiModifierBase[];
extern const std::size_t   kEmojiModifierBaseCount;
extern const std::uint32_t kEmojiComponent[];
extern const std::size_t   kEmojiComponentCount;
extern const std::uint32_t kExtendedPictographic[];
extern const std::size_t   kExtendedPictographicCount;

} // namespace ned::editor::grammar::compile::unicode

#endif // NED_EDITOR_GRAMMAR_COMPILE_UNICODETABLES_H
