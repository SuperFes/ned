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
#include <string_view>
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

// ---------------------------------------------------------------------------
// Per-capture-name styling (exhaustive-highlighting follow-up): the tier
// *below* SyntaxClass in specificity. A capture name is the raw dotted
// tree-sitter identity ("function.builtin.static"), and resolution walks it
// most-specific-first ("function.builtin.static" -> "function.builtin" ->
// "function"), each field taken from the most specific level that sets it,
// before falling through to the per-SyntaxClass override above and finally
// the built-in theme -- JetBrains' base-attributes-with-inheritance model,
// with SyntaxClass playing the base-attributes role. This per-*style* tier
// stays deliberately unscoped (a text-first editor's one foreground/
// background/trait rule set covers big and small languages alike -- an
// explicit user decision, not an oversight): a HighlightSpan carries a
// CaptureId, not a language, so there's no per-render-time hook to key a
// scoped lookup off without widening that struct. The class-remap tier
// below (SyntaxClassOverrideForCapture's language-taking overload) is
// different: it resolves at *parse* time, inside each Mode's own
// language-specific highlight closure, which already knows its own
// language for free -- see Mode.cpp's SyntaxClassForCapture, the seam a
// "<lang>/<name>" tier prepends to (storage is untouched: a caller can
// already configure a scoped override today via e.g.
// (ned/set-capture-class "markdown/punctuation.special" ...), this file
// only had to teach the *resolution* walk to try it).
//
// Capture names here are the same trust boundary SyntaxClass names cross:
// a malformed name (empty, leading '@', whitespace, or a leading/trailing/
// doubled '.') is a real bad Janet call and throws std::runtime_error; an
// unknown-but-well-formed name is fine -- overrides may be configured before
// the grammar that produces the name is ever loaded.
// ---------------------------------------------------------------------------

// Interning: stable CaptureId (Mode.h) per distinct name, never kNoCapture
// for a real name. Append-only for the process lifetime, same "load once,
// no teardown story" scope cut the dynamic-grammar dlopen handles already
// made. CaptureNameForId returns "" for kNoCapture or an id never handed
// out.
[[nodiscard]] CaptureId   InternCaptureName(std::string_view name);
[[nodiscard]] std::string CaptureNameForId(CaptureId id);

// Per-capture style overrides. Same field-by-field setter shape and hex
// validation as the SyntaxClass setters above; nullopt clears that field.
void SetCaptureForeground(const std::string& name, std::optional<std::string> hex);
void SetCaptureBackground(const std::string& name, std::optional<std::string> hex);
void SetCaptureBold(const std::string& name, std::optional<bool> value);
void SetCaptureItalic(const std::string& name, std::optional<bool> value);
void SetCaptureUnderlined(const std::string& name, std::optional<bool> value);
void SetCaptureStrikethrough(const std::string& name, std::optional<bool> value);

// Exact-name lookup, no inheritance walk -- the introspection counterpart
// of SyntaxOverrideFor above (every field nullopt when nothing is
// configured for exactly this name).
[[nodiscard]] SyntaxStyleOverride CaptureOverrideFor(const std::string& name);

// The inheritance walk described in the header comment above: each field
// taken from the most specific dotted level that sets it. ui::Theme::
// BrushFor applies the result on top of the SyntaxClass-level merge.
// Setters above bump SyntaxThemeGeneration() (same generation as the
// SyntaxClass setters -- both invalidate the same resolved-brush caching).
[[nodiscard]] SyntaxStyleOverride ResolvedCaptureOverride(std::string_view name);

// Capture -> SyntaxClass remapping: repoints what a capture name *is* (its
// base class, hence every built-in color/trait that class carries) rather
// than styling it field-by-field -- JetBrains' "inherit values from"
// control. Consulted by Mode.cpp's SyntaxClassForCapture at every dotted
// level, ahead of the built-in CaptureTable, so remapping "keyword" also
// re-bases "keyword.operator"'s fallback. nullopt clears. Its own
// generation, separate from SyntaxThemeGeneration(): a remap changes the
// classes baked into cached HighlightSpans (BufferView must re-run the
// highlight function), not just how a class renders (a cheap brush-cache
// flush).
void                                     SetSyntaxClassForCapture(const std::string& name, std::optional<SyntaxClass> cls);
[[nodiscard]] std::optional<SyntaxClass> SyntaxClassOverrideForCapture(std::string_view name);

// language-scoped-capture-rules follow-up: tries "<language>/<name>" first
// (language empty behaves exactly like the single-argument overload above),
// falling back to the plain unscoped lookup -- Mode.cpp's SyntaxClassForCapture
// is the sole caller, one per dotted specificity level it walks, so a
// language-scoped remap re-bases only that one grammar's use of a shared
// capture name (e.g. markdown's own "punctuation.special") without touching
// what every other bundled grammar's use of the same name resolves to.
[[nodiscard]] std::optional<SyntaxClass> SyntaxClassOverrideForCapture(std::string_view name, std::string_view language);
[[nodiscard]] std::size_t                CaptureClassGeneration();

// Every capture name this store has seen (interned, styled, or remapped),
// sorted -- merged with Mode.h's BuiltinCaptureNames() by ned/capture-names.
[[nodiscard]] std::vector<std::string> KnownCaptureNames();

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
