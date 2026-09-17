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

// Kind 6 (Blank lines): minimum enforced and maximum preserved blank lines
// immediately before a capture's own line -- FormattingCapabilities.md's
// "two independent halves, both needed". Deliberately "before" only for
// now: "around X" (JetBrains' own framing for e.g. "around method") is
// expressed as minBefore on the FOLLOWING sibling capture rather than as a
// separate minAfter on the preceding one, which would otherwise let two
// adjacent captures' independent edits collide at the one gap between them
// -- the same class of coincident-edit bug :within's empty-pair case
// already surfaced once (see FormatSpacing.cpp). minBefore is skipped
// outright when the capture is the first named child of its own immediate
// container (Mode.cpp's "<name>.first" marker, `FormatCapture::isFirst`)
// -- there is nothing above it to separate from but the container's own
// opening line, matching JetBrains' own separate (and usually off) "before
// first method" toggle. maxBefore is NOT gated on isFirst -- JetBrains'
// "keep maximum blank lines" is an unconditional cap applied everywhere,
// not a minimum-style exception.
struct BlankRuleValue {
    std::optional<int> minBefore;
    std::optional<int> maxBefore;
};

// Kind 4 (Wrap): a policy over a delimited LIST (a call's own arguments, an
// array literal's own elements) captured via Mode.h's own "<name>.item"
// convention (FormatCapture::items) -- FormattingCapabilities.md's own
// "policy over a list... plus the companion booleans every pane repeats."
// Only the two margin-INDEPENDENT policy values are implemented so far --
// `never` (always collapse to one line) and `always` (always one item per
// line) -- deliberately: `wrap-if-long`/`chop-down-if-long` need a real
// per-language wrap margin this rollout hasn't built yet (FillColumn.h is
// process-wide and prose-oriented, a different number in practice from a
// code line-length policy), logged as a real, scoped follow-up rather than
// guessed at. An unconfigured capture (no `policy` set) is a total no-op,
// the same "no built-in default, nothing forced" rule Break/Space/Blank
// already have -- which is what makes "keep existing line breaks" (JetBrains'
// own load-bearing default-on toggle) fall out for free: nothing touches a
// list's own line layout unless a policy is explicitly configured for it.
enum class WrapPolicy {
    Never,  // always collapse the list onto its own header's line
    Always, // always one item per line (chop-down), regardless of length
};

struct WrapRuleValue {
    std::optional<WrapPolicy> policy;
    // "Force trailing comma if multiline" -- only meaningful together with
    // `Always` (a `Never`-collapsed list never gets a trailing separator,
    // matching ordinary single-line call-site style). Left independently
    // optional rather than folded into `policy` since a future `if-long`
    // policy will want the exact same companion.
    std::optional<bool> forceTrailingComma;
};

// Kind 7 (Case): a token's own text case, keyed by a bare entity-kind
// string ("function", "parameter", "local", "type", "namespace", ...) --
// unlike Space/Break/Blank/Wrap, this is NOT a real `*-format.scm` capture
// name: FormatCase.h's own checker deliberately reuses EXISTING
// infrastructure (Mode::localScopes' locals.janet qualifiers,
// Mode::symbolKind's tags.janet SymbolKind buckets) rather than inventing
// a new query-capture convention, so the "name" CaseRuleFor resolves
// against is just the entity-kind label the checker itself assigns, not
// anything a query ever emits. The entity KIND (function vs. class vs.
// local) is still which key matched, not a separate keyed dimension,
// matching FormattingCapabilities.md's own "per entity kind" framing
// without inventing a second config shape alongside the flat string-keyed
// one Space/Break/Blank/Wrap already use -- same language-scoping
// convention too ("cpp/function" overrides the unscoped "function").
// Ten conventions, FormattingCapabilities.md's own catalogue verbatim
// (`<none>`, `lowercase`, `UPPERCASE`, `camelCase`, `PascalCase`,
// `snake_case`, `Leading_snake_case`, `Upper_Snake_Case`,
// `SCREAMING_SNAKE_CASE`, `lisp-case`) -- interpreted literally from that
// doc's own naming (each convention's own name IS a real example of
// itself, e.g. "Leading_snake_case" is itself leading-capitalized
// snake_case), not independently re-verified against a live JetBrains
// instance the way a grammar/compiler fact would be -- there is no
// external ground truth to check a NAMING SPEC against beyond the
// doc's own extraction. Deliberately NOT wired into the format-buffer/
// --format reformat pipeline at all: FormattingCapabilities.md's own
// stance is explicit -- "ship the checker first (a diagnostic), the
// fixer second (a code action). Never an automatic reformat step --
// renaming on save would be hostile."
enum class CaseConvention {
    None,               // <none> -- no constraint, always matches
    Lowercase,          // lowercase
    Uppercase,          // UPPERCASE
    CamelCase,          // camelCase
    PascalCase,         // PascalCase
    SnakeCase,          // snake_case
    LeadingSnakeCase,   // Leading_snake_case
    UpperSnakeCase,     // Upper_Snake_Case
    ScreamingSnakeCase, // SCREAMING_SNAKE_CASE
    LispCase,           // lisp-case
};

struct CaseRuleValue {
    std::optional<CaseConvention> convention;
};

// Kind 5 (Align): whether a run of adjacent, same-indent lines whose capture
// shares one name gets its anchor tokens padded to a shared column --
// FormattingCapabilities.md's own "a shared column across sibling lines".
// Unlike Space/Break/Blank/Wrap, a single capture is never enough on its own
// to decide anything: the grouping ITSELF (which adjacent lines form one
// alignable run) is a property of the whole capture list for one name, not
// of any one capture, so `enabled` is the only field -- there is no
// per-capture geometry left to configure once a construct opts in. See
// Editor/FormatAlign.h for the actual grouping rule (line-adjacent, same
// leading indent, FormattingCapabilities.md's own "adjacent lines forming a
// group" concept this rule kind is the first to need).
struct AlignRuleValue {
    std::optional<bool> enabled;
};

// Kind 8 (Arrange): reorder a run of adjacent sibling captures sharing one
// name by a sort key -- FormattingCapabilities.md's own "Reorder siblings by
// a key". Scoped to the Import-organisation half of that doc's B2 section
// for its pilot (Editor/FormatArrange.h); the Member-arrangement half
// (an ordered matching-rule list, grouping by "dependent"/"overridden") is
// explicitly out of scope for this rollout, per that doc's own "ship the
// ordering, degrade the two grouping rules" stance -- there is no ordered-
// rule-list field here to configure yet.
struct ArrangeRuleValue {
    std::optional<bool> enabled;
    // Ordinal byte compare by default (false/unset); true folds ASCII case
    // before comparing, JetBrains' own "sort case-insensitively" toggle.
    std::optional<bool> caseInsensitive;
};

// Kind 9 (Rewrite): replace a captured node with an equivalent one --
// FormattingCapabilities.md's own "replace a construct with an equivalent
// one", and its own explicit safety stance ("each ships off by default").
// Quote style is the pilot (Editor/FormatRewrite.h): a string literal's own
// delimiter character, `Single` (') or `Double` ("). Declined outright
// (left alone, never guessed) whenever rewriting would require re-escaping
// the string's own interior -- see FormatRewrite.cpp's own comment.
enum class QuoteStyle {
    Single, // '...'
    Double, // "..."
};

struct RewriteRuleValue {
    std::optional<QuoteStyle> quoteStyle;

    // A second, structurally unrelated rewrite family living in the same
    // per-capture value -- true rewrites an "elseif" keyword token to
    // "else if" (PHP's own `rewrite.elseif` pilot), independently of
    // quoteStyle. Each field is checked independently in
    // FormatRewrite.cpp's own ComputeRewriteEdits, the same "every optional
    // field its own independent lever" shape WrapRuleValue's own two fields
    // already use.
    std::optional<bool> expandElseif;
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

void SetCaseConvention(const std::string& name, std::optional<CaseConvention> value);

[[nodiscard]] CaseRuleValue CaseRuleFor(std::string_view name);
[[nodiscard]] CaseRuleValue CaseRuleFor(std::string_view name, std::string_view language);

void SetWrapPolicy(const std::string& name, std::optional<WrapPolicy> value);
void SetWrapForceTrailingComma(const std::string& name, std::optional<bool> value);

[[nodiscard]] WrapRuleValue WrapRuleFor(std::string_view name);
[[nodiscard]] WrapRuleValue WrapRuleFor(std::string_view name, std::string_view language);

void SetBlankMinBefore(const std::string& name, std::optional<int> value);
void SetBlankMaxBefore(const std::string& name, std::optional<int> value);

[[nodiscard]] BlankRuleValue BlankRuleFor(std::string_view name);
[[nodiscard]] BlankRuleValue BlankRuleFor(std::string_view name, std::string_view language);

void SetAlignEnabled(const std::string& name, std::optional<bool> value);

[[nodiscard]] AlignRuleValue AlignRuleFor(std::string_view name);
[[nodiscard]] AlignRuleValue AlignRuleFor(std::string_view name, std::string_view language);

void SetArrangeEnabled(const std::string& name, std::optional<bool> value);
void SetArrangeCaseInsensitive(const std::string& name, std::optional<bool> value);

[[nodiscard]] ArrangeRuleValue ArrangeRuleFor(std::string_view name);
[[nodiscard]] ArrangeRuleValue ArrangeRuleFor(std::string_view name, std::string_view language);

void SetRewriteQuoteStyle(const std::string& name, std::optional<QuoteStyle> value);
void SetRewriteExpandElseif(const std::string& name, std::optional<bool> value);

[[nodiscard]] RewriteRuleValue RewriteRuleFor(std::string_view name);
[[nodiscard]] RewriteRuleValue RewriteRuleFor(std::string_view name, std::string_view language);

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

// Same round-trip shape for WrapPolicy ("never"/"always").
[[nodiscard]] WrapPolicy  WrapPolicyByName(const std::string& name);
[[nodiscard]] std::string WrapPolicyName(WrapPolicy policy);

// Same round-trip shape for QuoteStyle ("single"/"double").
[[nodiscard]] QuoteStyle  QuoteStyleByName(const std::string& name);
[[nodiscard]] std::string QuoteStyleName(QuoteStyle style);

// Same round-trip shape for CaseConvention -- kebab-case names for the
// multi-word conventions, matching every other Janet-facing enum name in
// this codebase (BracePlacementByName's own "next-line-indented").
[[nodiscard]] CaseConvention CaseConventionByName(const std::string& name);
[[nodiscard]] std::string    CaseConventionName(CaseConvention convention);

// The pure check itself: whether `name` conforms to `convention`. `None`
// always matches; every other convention is interpreted literally from
// FormattingCapabilities.md's own naming (see CaseConvention's own header
// comment) via a straightforward structural scan, no regex. Empty input
// never matches anything but `None` -- a real identifier is never empty,
// so this only matters for a malformed/degenerate capture.
[[nodiscard]] bool MatchesCaseConvention(std::string_view name, CaseConvention convention);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATRULES_H
