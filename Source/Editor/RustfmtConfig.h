//
// A Rust project's rustfmt.toml (or .rustfmt.toml): the preferences rustfmt
// itself allows, applied to ned's own Rust formatting. Per buffer, from the
// nearest file above it -- hard_tabs/tab_spaces (indentation) and max_width
// (the ruler and fill column), laid over .editorconfig's by FileSettings.
// Per project, from the root's -- brace_style, control_brace_style and
// blank_lines_upper_bound, as Rust-scoped rules in the File layer under
// format.janet. Anything else rustfmt reads is left to rustfmt.
//

#ifndef NED_EDITOR_RUSTFMTCONFIG_H
#define NED_EDITOR_RUSTFMTCONFIG_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ned::editor {

struct RustfmtOptions {
    std::optional<bool>        hardTabs;
    std::optional<int>         tabSpaces;
    std::optional<int>         maxWidth;
    std::optional<std::string> braceStyle;        // SameLineWhere, PreferSameLine, AlwaysNextLine
    std::optional<std::string> controlBraceStyle; // AlwaysSameLine, ClosingNextLine, AlwaysNextLine
    std::optional<int>         blankLinesUpperBound;

    [[nodiscard]] bool operator==(const RustfmtOptions&) const = default;
};

// rustfmt.toml is flat `key = value`; a table header, an unknown key or a
// value of the wrong type is skipped, as rustfmt warns and carries on.
[[nodiscard]] RustfmtOptions ParseRustfmtToml(std::string_view text);

// The nearest rustfmt.toml or .rustfmt.toml in `directory` or above it.
[[nodiscard]] std::optional<std::filesystem::path> FindRustfmtConfig(const std::filesystem::path& directory);

// The options governing `file`: its nearest config's, nullopt with none.
[[nodiscard]] std::optional<RustfmtOptions> RustfmtOptionsFor(const std::filesystem::path& file);

// Writes the project root's brace and blank-line options as Rust-scoped
// rules into the File layer (ReloadFormatConfig clears and re-runs this).
void ApplyRustfmtRules(const std::filesystem::path& projectRoot);

} // namespace ned::editor

#endif // NED_EDITOR_RUSTFMTCONFIG_H
