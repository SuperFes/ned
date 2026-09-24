//
// A project's Prettier configuration: the options Prettier lets a project set
// that ned's own formatting follows. Per buffer, for a file Prettier formats,
// from the nearest config above it -- useTabs/tabWidth (indentation) and
// printWidth (the ruler and fill column), laid over .editorconfig's by
// FileSettings, as Prettier itself does. Per project, from the root's --
// singleQuote, as the JavaScript/TypeScript quote rule in the File layer
// under format.janet. A config written as JavaScript can't be read without
// running it, so it counts as none; `overrides` blocks are not applied.
//

#ifndef NED_EDITOR_PRETTIERCONFIG_H
#define NED_EDITOR_PRETTIERCONFIG_H

#include <filesystem>
#include <optional>
#include <string_view>

namespace ned::editor {

struct PrettierOptions {
    std::optional<bool> useTabs;
    std::optional<int>  tabWidth;
    std::optional<int>  printWidth;
    std::optional<bool> singleQuote;

    [[nodiscard]] bool operator==(const PrettierOptions&) const = default;
};

// A config's own text, JSON (`{...}`, comments allowed), YAML or TOML --
// flat `key: value` / `key = value` lines. An option of the wrong type is
// skipped; JSON that doesn't parse is nullopt, as if there were no config.
[[nodiscard]] std::optional<PrettierOptions> ParsePrettierConfig(std::string_view text);

// The nearest Prettier config at or above `directory`: a `.prettierrc` file
// of any readable spelling, or a package.json with a "prettier" object.
// nullopt when there is none, or the nearest one can't be read (JavaScript).
[[nodiscard]] std::optional<PrettierOptions> PrettierOptionsIn(const std::filesystem::path& directory);

// The options governing `file` when Prettier formats its kind of file.
[[nodiscard]] std::optional<PrettierOptions> PrettierOptionsFor(const std::filesystem::path& file);

// Writes the project root's quote style as JavaScript-, TypeScript- and
// TSX-scoped rules into the File layer (ReloadFormatConfig clears and
// re-runs this). Prettier defaults to double quotes, so a config without
// singleQuote asks for those.
void ApplyPrettierRules(const std::filesystem::path& projectRoot);

} // namespace ned::editor

#endif // NED_EDITOR_PRETTIERCONFIG_H
