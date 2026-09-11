//
// class-file-sync follow-up: keeping a file's name and the type declared
// inside it in agreement -- JetBrains' most-used refactor after rename
// itself. Renaming "class Widget" in Widget.php offers to rename the file;
// renaming the file offers to rename the class.
//
// Pure and buffer-free, the same split Editor/LocalScopes.h,
// Editor/RenameReview.h and Text/ThreeWayMerge.h already take: everything
// here works over a flat SymbolMarker list (Mode::symbolKind, from the
// language's own tags.scm) plus a filename, so it is unit-testable with no
// Parser, no Buffer and no Screen. The I/O half -- reading the buffer,
// prompting, and handing the file rename to PerformProjectRename so the
// import fixup and workspace/willRenameFiles come along -- lives in
// UI/BufferView/ClassFileSync.cpp.
//
// TWO STRICTNESS TIERS, and the split is the whole design. Strictness itself
// (SelectType) decides only ONE thing -- what to do when a file declares
// several top-level types:
//
//   Strict     refuses. It is what ned offers unprompted, and an unasked-for
//              prompt naming one of several types the user never pointed at
//              is worse than no feature.
//   BestEffort takes the outermost and reports how many there were, so the
//              caller can name the one it picked. It is what the user's own
//              command asks for: asking IS the missing certainty.
//
// The rest of each tier's caution lives in the two offer predicates below
// (FileRenameFollowsSymbolRename, TypeRenameFollowsFileMove), not in the enum
// -- they additionally require that the file and the type agreed BEFORE the
// rename, which is what makes an unprompted offer safe. Everything else here
// is tier-independent: SplitFileName/FileNameForType preserve a compound
// suffix either way (Thing.class.php keeps ".class.php", foo.d.ts keeps
// ".d.ts"), and either tier finding nothing means nothing -- the caller says
// so and stops rather than inventing a name.
//
// Deliberately not a judgement about naming CONVENTION. Whether a file
// "should" be named after its type is a per-project, per-language question
// this has no way to answer, so an unprompted offer only ever acts on a file
// that was ALREADY named after the type beforehand -- evidence from the
// project itself, not a rule imposed on it.
//
// One consequence of where the offers are wired: renaming a top-level type is
// always a cross-file edit, so rename-symbol's scope-aware tier declines it
// outright and the offer rides on a language server's rename landing, or on a
// *rename* review being committed. With no server for a language the explicit
// commands are the whole feature -- and they need none.
//

#ifndef NED_EDITOR_CLASSFILESYNC_H
#define NED_EDITOR_CLASSFILESYNC_H

#include "Editor/Mode.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor::classfile {

// A filename split at its FIRST dot rather than its last, so a compound
// suffix survives a stem swap: "Thing.class.php" is {"Thing", ".class.php"},
// not {"Thing.class", ".php"} as std::filesystem::path::stem/extension would
// have it. That is what lets the PEAR-era naming still in the wild round-trip
// through a rename, and it covers the modern spellings of the same idea
// (foo.d.ts, Widget.test.tsx) with one rule instead of a suffix table.
//
// A leading dot is part of the stem, never the start of a suffix, so
// ".gitignore" is {".gitignore", ""} rather than {"", ".gitignore"}.
struct FileNameParts {
    std::string stem;
    std::string suffix; // includes its leading '.', empty when there is none
};

[[nodiscard]] FileNameParts SplitFileName(std::string_view filename);

// filename with its stem replaced by typeName, suffix untouched. Empty when
// typeName could not be a filename at all (empty, or carrying a path
// separator) -- the caller declines rather than writing something odd.
[[nodiscard]] std::string FileNameForType(std::string_view filename, std::string_view typeName);

// One candidate type declaration: its name, and the byte range of the whole
// declaration (which the caller uses to put point on it before starting a
// rename).
struct TypeMatch {
    std::string name;
    std::size_t startByte = 0;
    std::size_t endByte   = 0;
    // Where the identifier itself sits, not the declaration -- what a
    // rename of this type actually edits, and where point goes before
    // handing over to the ordinary rename flow.
    std::size_t nameStartByte = 0;
};

enum class Decline {
    None,         // a type was selected
    NoTypes,      // nothing type-like at the top level (or no tags query at all)
    SeveralTypes, // more than one, and the strict tier refuses to guess
};

struct Resolution {
    std::optional<TypeMatch> type;
    Decline                  decline        = Decline::NoTypes;
    std::size_t              candidateCount = 0; // top-level types found, however many were used
};

// Every type-like definition that no OTHER definition encloses, in source
// order. Two rules, both load-bearing:
//
//   - Namespace markers never count as enclosing. A block-form C# or
//     TypeScript namespace wraps the file's types, so counting it as a
//     container would make every namespaced file report its namespace as
//     "the type"; the statement forms (PHP's "namespace App;", C# 10's
//     file-scoped "namespace App;") do not wrap anything at all, so counting
//     them as types would make every such file report two. Skipping them for
//     both purposes is what makes the two spellings behave identically.
//   - Everything else does count as enclosing, Callable included. A class
//     declared inside a function (ordinary in JavaScript) is not the file's
//     type, and byte containment is the only thing that says so.
[[nodiscard]] std::vector<TypeMatch> TopLevelTypes(std::span<const SymbolMarker> markers);

enum class Strictness {
    Strict,     // exactly one top-level type, or nothing
    BestEffort, // the outermost when there are several, reporting the count
};

[[nodiscard]] Resolution SelectType(std::span<const SymbolMarker> markers, Strictness strictness);

// Direction A's automatic tier: a symbol rename just landed in this file,
// turning `renamedFrom` into `renamedTo`. True only when the file held
// exactly one top-level type, that type is now named `renamedTo`, and the
// filename's stem is still `renamedFrom` -- i.e. the file WAS named after
// this type and no longer is, which is the one situation where the offer is
// plainly right rather than a guess about convention.
[[nodiscard]] bool FileRenameFollowsSymbolRename(const Resolution& resolution, std::string_view filename,
                                                 std::string_view renamedFrom, std::string_view renamedTo);

// Direction B's automatic tier: a file just moved from `previousFilename` to
// `newFilename`. The mirror of the rule above -- exactly one top-level type,
// still named after the OLD filename's stem, with a new stem that differs and
// could be a type name at all.
[[nodiscard]] bool TypeRenameFollowsFileMove(const Resolution& resolution, std::string_view previousFilename,
                                             std::string_view newFilename);

// Whether a string could be a type identifier in any of the languages this
// covers -- a conservative "letters, digits, underscore, and any byte >= 0x80,
// not starting with a digit" test, the same permissive shape
// RenameReview.h's own word-boundary rule takes rather than a per-language
// charset. Used to refuse proposing a type name derived from a filename that
// plainly is not one ("my-component", "02_migration").
[[nodiscard]] bool LooksLikeTypeName(std::string_view text);

} // namespace ned::editor::classfile

#endif // NED_EDITOR_CLASSFILESYNC_H
