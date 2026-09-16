//
// configurable-formatter follow-up: ned/set-indent-rule -- a capture-scoped
// per-construct override of what Indent.h's own generic tree-walk engine
// would otherwise compute for a line, for the cases that engine has no
// other way to express: a line that should sit a fixed number of columns
// off whatever level its surroundings would otherwise give it (a C++ access
// specifier sitting -2 from its class body -- {policy: offset, value: -2}),
// or one that should ignore nesting depth entirely (a goto label or
// preprocessor directive, root-scoped regardless of surrounding nesting --
// {policy: absolute, value: 0}).
//
// Keyed by GRAMMAR NODE TYPE (e.g. "access_specifier" -- the same string
// Grammar/Node.h's Node::Type() and GrammarImprint.cpp's own table already
// use), not a highlight or format-rule capture name. This is a deliberate
// v1 scope cut, not the "hybrid capture-name/grammar-type keying" the
// original design sketch described: most indent-contributing nodes come
// from the delimiter imprint (Editor/ImprintIndent.h), not a query capture,
// and have no capture name to hang an override on at all -- confirmed via
// both of this feature's own worked examples, neither of which has one.
// Capture-name keying (for the minority of constructs an indents.janet
// query DOES name) would need mode.formatCaptures threaded into
// BuildIndentFunction's own construction, which no existing caller does
// today -- deferred until a real need shows up, rather than plumbed in on
// spec for a hybrid this file cannot yet justify concretely.
//
// Applied in Indent.cpp's BuildIndentFunction as a small, ADDITIVE final
// step -- resolving the line's own smallest named node at its first non-
// blank byte (the same NamedDescendantForByteRange call IndentLevelForLine's
// own walk-start resolution already makes, just for a fresh position here)
// and checking THIS lookup against it -- rather than woven into
// IndentLevelForLine's own ancestor-counting walk, so the engine's existing,
// heavily-tested per-language behavior (Tests/IndentEngineTest.cpp,
// Tests/ImprintIndentTest.cpp, and 19 languages' own indents.janet files)
// is completely untouched when no rule is configured for anything a
// document's tree actually contains.
//
// Same mutex-guarded-static-state, "<language>/<key>" scoped-beats-unscoped
// resolution every FormatRules.h rule kind already uses (ScopedRuleFor's own
// shape) -- but a SINGLE combined (policy, value) pair per key rather than
// several independently-settable fields, since Offset and Absolute are
// mutually exclusive, not independent axes.
//

#ifndef NED_EDITOR_INDENTRULEOVERRIDE_H
#define NED_EDITOR_INDENTRULEOVERRIDE_H

#include <optional>
#include <string>
#include <string_view>

namespace ned::editor {

enum class IndentRulePolicy {
    Offset,   // value is added, in columns, to whatever column the ordinary walk computed
    Absolute, // value IS the column, outright -- the ordinary walk's own result is discarded
};

struct IndentRuleValue {
    std::optional<IndentRulePolicy> policy;
    int                             value = 0; // meaning depends on policy; unused when policy is nullopt
};

// Sets (std::nullopt clears) key's own override. key is validated the same
// way FormatRules.h's own capture-name setters are (no leading '@'/'.', no
// "..", no whitespace) even though a grammar node type never has dots --
// the one shared guard against an obviously-wrong argument, not a
// capture-name-specific rule.
void SetIndentRule(const std::string& key, std::optional<IndentRuleValue> value);

// Default-constructed (policy = nullopt) when key has no configured
// override -- BuildIndentFunction's own "nothing to do" fast path.
[[nodiscard]] IndentRuleValue IndentRuleFor(std::string_view key);

// language-scoped lookup: "<language>/<key>" wins over the bare key.
[[nodiscard]] IndentRuleValue IndentRuleFor(std::string_view key, std::string_view language);

} // namespace ned::editor

#endif // NED_EDITOR_INDENTRULEOVERRIDE_H
