//
// Module names that are file names wherever the file sits: OCaml's and
// ReScript's modules, Pascal units, Ada's GNAT-named units, VHDL design
// units by convention. Import resolution's `:flat-modules` key
// (ImportResolutionConfig.h).
//

#ifndef NED_EDITOR_FLATMODULES_H
#define NED_EDITOR_FLATMODULES_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

// The file under root named `name` plus one of `extensions`, compared
// case-insensitively; the first extension that any file has wins, and
// among files with it the lexically first path. Dot-directories and
// .gitignored ones are skipped, and the walk stops after a bounded number of
// entries so a root that is really $HOME answers nothing rather than slowly.
[[nodiscard]] std::optional<std::filesystem::path> FindFlatModule(std::string_view                name,
                                                                  const std::vector<std::string>& extensions,
                                                                  const std::filesystem::path&    root);

} // namespace ned::editor

#endif // NED_EDITOR_FLATMODULES_H
