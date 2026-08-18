//
// Per-SyntaxClass style overrides, settable from Janet -- the "each
// themeable entry should have any possible theme" mechanism: one generic,
// name-keyed store covering every SyntaxClass (Mode.h) uniformly, rather
// than one-off code per class. Editor-layer, not UI: this is what lets
// Source/Janet/EditorBindings.cpp reach a styling concept without
// Source/Editor/ ever depending on Source/UI/Theme.h, the same direction
// every existing cross-layer dependency in this codebase already goes (UI
// depends on Editor, never the reverse) -- ui::Theme::BrushFor() is the
// consumer, merging these on top of whichever built-in Dark/Light theme is
// active. Colors are stored as raw "#rrggbb" hex-token strings (matching
// ThemeFile.cpp's own existing wire format exactly), not a ui::Color --
// deliberately UI-agnostic.
//
// Mutex-guarded static state, mirroring TabWidth.h/CodeFoldSettings.h's
// exact pattern.
//
// Nil vs. throw: a getter (SyntaxOverrideFor, or the per-field ned/syntax-*
// Janet accessors built on top of it) returns "unset" (std::nullopt, which
// Janet sees as nil) for a class that simply has no override configured
// yet -- that's the common, everyday case, not an error. An unrecognized
// SyntaxClass *name* (SyntaxClassByName) is a genuinely different
// situation -- a real bad Janet call -- and throws std::runtime_error,
// matching this codebase's established "a bad call surfaces as a real
// error, not silent failure" convention (e.g. RegisterDynamicMode).
//

#ifndef NED_EDITOR_SYNTAXTHEME_H
#define NED_EDITOR_SYNTAXTHEME_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Mode.h"

namespace ned::editor {

struct SyntaxStyleOverride {
    std::optional<std::string> foreground; // "#rrggbb"
    std::optional<std::string> background; // "#rrggbb"
    std::optional<bool>        bold;
    std::optional<bool>        italic;
    std::optional<bool>        underlined;
    std::optional<bool>        strikethrough;
};

// hex must be "#" followed by exactly 6 hex digits, or nullopt (clears any
// existing override for that field) -- throws std::runtime_error otherwise.
void SetSyntaxForeground(SyntaxClass cls, std::optional<std::string> hex);
void SetSyntaxBackground(SyntaxClass cls, std::optional<std::string> hex);
void SetSyntaxBold(SyntaxClass cls, std::optional<bool> value);
void SetSyntaxItalic(SyntaxClass cls, std::optional<bool> value);
void SetSyntaxUnderlined(SyntaxClass cls, std::optional<bool> value);
void SetSyntaxStrikethrough(SyntaxClass cls, std::optional<bool> value);

// Every field left at nullopt if nothing has been configured for cls at
// all -- never throws (cls is a real enum value here, not a name that
// could be misspelled).
[[nodiscard]] SyntaxStyleOverride SyntaxOverrideFor(SyntaxClass cls);

// Bumped by every setter above -- mirrors FoldGeneration()'s own "cheap,
// did-it-change" signal shape, for a future cache if a real
// [Performance] test ever asks for one (see this file's own header
// comment on why BrushFor() doesn't cache this yet).
[[nodiscard]] std::size_t SyntaxThemeGeneration();

// Kebab-case name <-> SyntaxClass, e.g. "comment"/"doc-comment"/
// "control-keyword" -- matching every other Janet-facing name convention
// in this codebase. SyntaxClassByName throws std::runtime_error for an
// unrecognized name (see this file's own header comment on the nil-vs-throw
// split).
[[nodiscard]] SyntaxClass              SyntaxClassByName(const std::string& name);
[[nodiscard]] std::string              SyntaxClassName(SyntaxClass cls);
[[nodiscard]] std::vector<std::string> SyntaxClassNames(); // sorted, every valid name

} // namespace ned::editor

#endif // NED_EDITOR_SYNTAXTHEME_H
