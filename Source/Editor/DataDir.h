//
// Where ned's installed data lives: `<data dir>/languages/<name>/` (the
// bundled language definitions and query files) and `<data dir>/plugins/`
// (the bundled Janet plugins). Nothing under it is compiled into the binary.
//
// Resolution, first match wins:
//   1. $NED_DATA_DIR -- authoritative when set: a value that does not hold a
//      data tree is an error, never a silent fall-through.
//   2. `<directory of the running executable>/../share/ned` -- the same rule
//      resolves build/Source/ned and build/Tests/ned_tests to the build tree's
//      share/ned (CMake/DataTree.cmake assembles it) and /usr/bin/ned to
//      /usr/share/ned, so a relocated prefix keeps working.
//   3. The configured install location (CMAKE_INSTALL_FULL_DATADIR/ned).
//
// "Holds a data tree" means a `languages` subdirectory exists.
//

#ifndef NED_EDITOR_DATADIR_H
#define NED_EDITOR_DATADIR_H

#include <filesystem>
#include <optional>

namespace ned::editor {

struct DataDirCandidates {
    std::optional<std::filesystem::path> environment;
    std::optional<std::filesystem::path> executableRelative;
    std::filesystem::path                installed;
};

// The process's own candidates: $NED_DATA_DIR, /proc/self/exe's parent,
// the compiled-in install path.
[[nodiscard]] DataDirCandidates DefaultDataDirCandidates();

// Throws std::runtime_error naming every candidate looked at when none
// holds a data tree.
[[nodiscard]] std::filesystem::path ResolveDataDir(const DataDirCandidates& candidates);

// ResolveDataDir(DefaultDataDirCandidates()), resolved once per process.
[[nodiscard]] const std::filesystem::path& DataDir();

} // namespace ned::editor

#endif // NED_EDITOR_DATADIR_H
