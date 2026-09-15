//
// configurable-formatter-rules follow-up: per-capture-name overrides for the
// formatter's Space (kind 2) and Break (kind 3, brace placement folded in as
// its specialised case -- see Docs/FormattingCapabilities.md) rule kinds,
// storage/resolution shape mirroring SyntaxTheme.h's per-capture-name tier
// exactly -- a mutex-guarded static store keyed by the raw dotted capture
// name a `*-format.scm` query names (no leading '@'), with a language-scoped
// overload trying "<language>/<name>" before falling back to the unscoped
// entry, matching SyntaxClassOverrideForCapture(name, language)'s own shape.
// That is what lets a rule be written once ("space before control.parens")
// and overridden narrowly for one language's own quirk
// ("cpp/control.parens") without duplicating the shared table.
//
// Deliberately flat/exact-match, not a dotted-name inheritance walk --
// SyntaxTheme's *remap* tier's shape (SetSyntaxClassForCapture), not its
// *styling* tier's (ResolvedCaptureOverride). A `*-format.scm` capture is
// expected to name one specific construct each (`control.parens`,
// `brace.function`) rather than form a specificity hierarchy the way a
// highlight capture's dotted modifiers do; revisit if that assumption
// doesn't hold once real query files exist.
//
// This file is the resolution engine only -- no `*-format.scm` query kind
// exists yet (LanguageDefinition.h's QueryFiles has no `format` member), and
// no pass consumes these values yet. That is deliberate: the concrete rule
// catalogue (which capture names exist, which fields each construct needs)
// is still being scoped. What's here is useful on its own -- format.janet's
// :space/:break schema and ned/set-format-* already round-trip through it --
// and is the seam the real passes attach to once that catalogue lands.
//

#ifndef NED_EDITOR_FORMATRULES_H
#define NED_EDITOR_FORMATRULES_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ned::editor {

// Kind 2 (Space): whether a space is inserted at each position around or
// inside the captured token/delimiter pair. `within` is "just inside a
// delimiter pair" -- covers both the empty-pair case ("()" vs "( )") and the
// non-empty one ("(x)" vs "( x )"), matching every JetBrains pane's own
// "Within: parentheses/brackets/braces/..." grouping (see
// Docs/FormattingCapabilities.md's Spaces section).
struct SpaceRuleValue {
    std::optional<bool> before;
    std::optional<bool> after;
    std::optional<bool> within;
};

// Kind 3 (Break), brace placement folded in as its specialised case rather
// than a tenth kind -- FormattingCapabilities.md's own framing ("Brace
// placement (kind 3, specialised)"). `placement`/`collapseEmpty`/
// `collapseSimple` are meaningful only on a capture that names a
// brace-carrying construct; `before`/`after` are the general mandatory/
// forbidden-newline rule ("place 'else' on a new line after a compound
// statement", "'while' on new line in do-while").
enum class BracePlacement {
    SameLine,         // K&R -- opening brace on the same line as its header
    NextLine,         // Allman -- opening brace alone on the next line
    NextLineIndented, // GNU/Whitesmiths -- next line, indented one level
};

struct BreakRuleValue {
    std::optional<bool>           before;
    std::optional<bool>           after;
    std::optional<BracePlacement> placement;
    std::optional<bool>           collapseEmpty;  // keep empty braces/block on one line
    std::optional<bool>           collapseSimple; // keep a simple one-statement block on one line
};

// Malformed vs. merely unknown follows SyntaxTheme.h's own trust-boundary
// split: an empty name, a leading '@', a leading/trailing/doubled '.', or
// embedded whitespace is a real bad call and throws std::runtime_error; an
// unknown-but-well-formed name is fine -- rules may be configured before the
// language/query that produces the name is ever loaded.

void SetSpaceBefore(const std::string& name, std::optional<bool> value);
void SetSpaceAfter(const std::string& name, std::optional<bool> value);
void SetSpaceWithin(const std::string& name, std::optional<bool> value);

// Exact-name lookup, no language scoping or inheritance walk.
[[nodiscard]] SpaceRuleValue SpaceRuleFor(std::string_view name);
// Tries "<language>/<name>" first (language empty behaves exactly like the
// single-argument overload above), falling back to the plain unscoped
// lookup -- SyntaxClassOverrideForCapture(name, language)'s exact shape.
[[nodiscard]] SpaceRuleValue SpaceRuleFor(std::string_view name, std::string_view language);

void SetBreakBefore(const std::string& name, std::optional<bool> value);
void SetBreakAfter(const std::string& name, std::optional<bool> value);
void SetBracePlacement(const std::string& name, std::optional<BracePlacement> value);
void SetBraceCollapseEmpty(const std::string& name, std::optional<bool> value);
void SetBraceCollapseSimple(const std::string& name, std::optional<bool> value);

[[nodiscard]] BreakRuleValue BreakRuleFor(std::string_view name);
[[nodiscard]] BreakRuleValue BreakRuleFor(std::string_view name, std::string_view language);

// Bumped by every setter above -- one counter for both kinds, mirroring
// SyntaxThemeGeneration()'s own "cheap, did-it-change" signal shape (the
// SyntaxClass/capture-style tiers share one generation too).
[[nodiscard]] std::size_t FormatRuleGeneration();

// kebab-case <-> BracePlacement ("same-line"/"next-line"/"next-line-indented"),
// matching every other Janet-facing name convention (SyntaxClassByName's own
// shape). BracePlacementByName throws std::runtime_error for an unrecognized
// name.
[[nodiscard]] BracePlacement BracePlacementByName(const std::string& name);
[[nodiscard]] std::string    BracePlacementName(BracePlacement placement);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATRULES_H
