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
// (tags.janet, already resolves function/type/namespace declarations)
// between them cover a real, if partial, entity-kind set with zero new
// query authoring. `Editor/LocalScopes.h`'s own `LocalCapture` and
// `Mode.h`'s own `SymbolMarker` are read directly here -- no new
// per-language format.janet capture is needed for this rule kind at all.
//
// Entity-kind granularity today is coarser than FormattingCapabilities.md's
// own full catalogue (class/struct/interface/enum/function/method/field/
// parameter/local/global/constant/macro/namespace/type-alias/...):
// `SymbolKind::Callable` conflates free functions with in-class methods,
// and `SymbolKind::TypeLike` conflates class/struct/type-alias/enum --
// confirmed via a real background investigation of cpp's own tags.janet/
// locals.janet before writing this, not assumed. Five entity kinds are
// piloted here: "parameter"/"local" (locals.janet), "function"/"type"/
// "namespace" (tags.janet, at SymbolMarker's own bucketing). Field/
// global/constant/macro/enum-member/template-parameter/a function-vs-
// method split all need new query authoring this file does not attempt --
// a real, documented scope cut, not silently missing.
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
// CaseRuleFor(entityKind, languageKey).convention. `entityKind` is one of
// "parameter"/"local"/"function"/"type"/"namespace" today (see this
// file's own header comment on why not the full catalogue).
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
