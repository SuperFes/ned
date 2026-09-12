#include "Imprint.h"

namespace ned::editor::imprint {

std::string DelimiterKindName(DelimiterKind kind) {
    switch (kind) {
        case DelimiterKind::Bracket: return "Bracket";
        case DelimiterKind::Indent:  return "Indent";
    }
    return "?";
}

bool ShouldFold(const DelimitedBody& body, const FoldPolicy& policy) {
    // A body holding exactly one subexpression has nothing to collapse:
    // `parenthesized_expression`, `decltype(x)`, `index_expression`.
    if (!body.listLikeInterior) {
        return false;
    }
    // An argument/parameter list is the list-like bracketed body that does
    // not open its own production -- the callee or declarator precedes it.
    // That is precisely the separable case the policy exists for; every
    // statement/member block opens with its own brace.
    if (!body.openerIsFirst && !policy.foldArgumentLists) {
        return false;
    }
    return true;
}

} // namespace ned::editor::imprint
