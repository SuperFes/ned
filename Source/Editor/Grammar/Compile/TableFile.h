//
// A compiled language on disk: the `tables` file of a language package.
// A versioned little-endian byte stream written field by field (never a
// struct image, so the union layouts in Parse/Abi.h stay a private detail
// of the engine), read back into a CompiledLanguage in one pass.
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_TABLEFILE_H
#define NED_EDITOR_GRAMMAR_COMPILE_TABLEFILE_H

#include <memory>
#include <string>
#include <string_view>

#include "Editor/Grammar/Compile/Tables.h"

namespace ned::editor::grammar::compile {

inline constexpr std::string_view kTableFileMagic   = "NEDTABLE";
inline constexpr std::uint32_t    kTableFileVersion = 1;

[[nodiscard]] std::string SerializeLanguage(const CompiledLanguage& language);

// Throws CompileError for a file that is not a table file, was written by a
// different format version, or is truncated or inconsistent.
[[nodiscard]] std::unique_ptr<CompiledLanguage> LoadLanguage(std::string_view bytes);

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_TABLEFILE_H
