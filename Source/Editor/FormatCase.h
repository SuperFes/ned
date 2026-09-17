//
// case-kind follow-up (kind 7 of Docs/FormattingCapabilities.md's nine-
// rule-kind catalogue): naming conventions. Architecturally DIFFERENT
// from every rule kind before it -- the capabilities doc's own stance is
// explicit ("ship the checker first (a diagnostic), the fixer second (a
// code action). Never an automatic reformat step -- renaming on save
// would be hostile"), so this is deliberately NOT another `Compute*Edits`
// pass wired into format-buffer/--format. This file is the checker half
// only: a pure, buffer-free scan producing violations, no Buffer/Parser/
// Screen dependency, the same split LocalScopes.h/RenameReview.h already
// establish for exactly this reason.
//
// Reuses EXISTING infrastructure rather than inventing a new query-
// capture convention: FormattingCapabilities.md's own words, "we are
// closer to this than it looks" -- `Mode::localScopes` (locals.janet,
// already resolves parameter/local bindings) and `Mode::symbolKind`
// (tags.janet, already resolves definition-site declarations) between
// them cover the entity-kind set below. `Editor/LocalScopes.h`'s own
// `LocalCapture` and `Mode.h`'s own `SymbolMarker` are read directly here
// -- no new per-language *format.janet* capture convention is needed for
// this rule kind at all (a language's tags.janet/locals.janet may still
// gain new patterns, as cpp's did, but that's the existing tags.scm
// convention, not something invented for this file).
//
// case-catalogue follow-up: entity-kind granularity now tracks
// SymbolMarker::definitionKind (the tags query's own "@definition.*"
// capture suffix, verbatim) rather than SymbolKind's coarse 3-4-bucket
// grouping -- see Mode.h's own doc comments on both. What actually reaches
// this file's entity-kind set is still bounded by what a language's own
// tags.janet/locals.janet distinguishes: "parameter"/"local" (locals.janet)
// plus whatever "@definition.*" words a language's tags.janet emits.
// cpp's own tags.janet was widened alongside this file to emit "class"/
// "struct"/"enum"/"enum_member"/"field"/"macro"/"function"/"method"/
// "namespace"/"template_parameter" -- see Languages/cpp/tags.janet's own
// header comment for exactly which grammar shapes each pattern covers and
// which it deliberately doesn't (a const/constexpr-qualified "constant"
// bucket, a top-level "global" entity kind -- tried, reverted, see that
// file's own comment on why). Other bundled languages get whatever their
// own upstream tags.scm already emitted verbatim, at zero new query-
// authoring cost -- e.g. Python's/JavaScript's own "constant", Rust's own
// "macro", PHP's own "field", Kotlin's own "var" (renamed "global" here,
// same as any future language's own free-standing-variable capture would
// be) -- since EntityKindForMarker (FormatCase.cpp) passes any capture
// suffix it doesn't have a specific rename for straight through unchanged.
// A language with no tags.janet at all, or one nobody has widened yet,
// only ever surfaces what it already did before this follow-up -- strictly
// additive, never a regression for a language nobody's touched.
//

#ifndef NED_EDITOR_FORMATCASE_H
#define NED_EDITOR_FORMATCASE_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "FormatRules.h"
#include "Mode.h"

namespace ned::editor {

// One name that doesn't conform to its entity kind's configured
// CaseRuleFor(entityKind, languageKey).convention. `entityKind` is
// "parameter"/"local" (from locals.janet) or whatever "@definition.*"
// word a language's own tags.janet emits, e.g. "class"/"struct"/"enum"/
// "enum_member"/"field"/"global"/"macro"/"function"/"method"/"namespace"/
// "template_parameter"/"type" (see this file's own header comment for
// which languages emit which today).
// `suggestedName` is a best-effort conforming rewrite (empty only if the
// name tokenizes to nothing usable, e.g. an all-punctuation operator
// name) -- computed here, not left for a caller/fixer to guess, since a
// fixer needs it ready-made rather than asking the user to type one
// (FormattingCapabilities.md's own framing: the checker is a diagnostic,
// the fixer just applies what the checker already decided).
struct CaseViolation {
    std::string entityKind;
    std::string name;
    std::size_t nameStartByte;
    std::size_t nameEndByte;
    CaseConvention expectedConvention;
    std::string    suggestedName;
};

// Scans `mode`'s own localScopes/symbolKind output (both optional --
// silently contributes nothing for whichever is unset, matching every
// other rule kind's "no built-in default, nothing forced" contract) for
// every entity whose kind has a CaseRuleFor(entityKind, languageKey)
// convention configured, testing each one's own name against it via
// MatchesCaseConvention. An entity kind with no configured convention is
// never scanned at all -- same "unconfigured is a total no-op" rule
// every other rule kind here follows.
[[nodiscard]] std::vector<CaseViolation> ComputeCaseViolations(std::string_view text, std::string_view languageKey,
                                                               const Mode& mode);

// The pure rename-suggestion half on its own, exposed separately since a
// future interactive fixer wants to recompute it against a possibly-
// edited name without re-running the whole scan. Tokenizes `name` on
// underscore/hyphen separators and camelCase-style lower-to-upper
// transitions (a best-effort split, not a perfect one -- an acronym run
// like "HTMLParser" tokenizes as ["HTML", "Parser"], matching common
// real-world naming-convention tooling's own accepted imprecision here),
// then re-joins the resulting words per `convention`'s own casing/
// separator rule. Returns `name` unchanged if it already matches, and an
// empty string if `name` tokenizes to no usable words at all (e.g. a
// pure-operator name like "operator+").
[[nodiscard]] std::string SuggestNameForConvention(std::string_view name, CaseConvention convention);

} // namespace ned::editor

#endif // NED_EDITOR_FORMATCASE_H
