// Range tables for the derived identifier properties -- see
// Tools/gen-unicode-tables.py, which generates UnicodeTables.cpp. Each table
// is a flat list of half-open [start, end) pairs, ascending and disjoint.

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

} // namespace ned::editor::grammar::compile::unicode

#endif // NED_EDITOR_GRAMMAR_COMPILE_UNICODETABLES_H
