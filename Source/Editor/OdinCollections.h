//
// Odin's collection imports (`import "core:fmt"`): a collection names a
// directory, the toolchain's own (base, core, vendor, shared under
// ODIN_ROOT) or one the project's ols.json declares. Import resolution's
// `:odin-collections` key (ImportResolutionConfig.h).
//

#ifndef NED_EDITOR_ODINCOLLECTIONS_H
#define NED_EDITOR_ODINCOLLECTIONS_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ned::editor {

// The collection name of a `name:path` import, or nullopt for a plain path.
[[nodiscard]] std::optional<std::string> OdinCollectionName(std::string_view importPath);

// The directory collection `name` stands for from searchStart: the nearest
// ols.json's "collections" entry, else the toolchain's own under odinRoot.
// Empty when neither knows it.
[[nodiscard]] std::filesystem::path OdinCollectionRoot(std::string_view name, const std::filesystem::path& searchStart,
                                                       const std::filesystem::path& odinRoot);

// $ODIN_ROOT, else `odin root` (asked once per process). Empty when neither
// says.
[[nodiscard]] std::filesystem::path OdinRoot();

} // namespace ned::editor

#endif // NED_EDITOR_ODINCOLLECTIONS_H
