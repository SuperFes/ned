#include "GrammarImprint.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace ned::editor::treesitter {

namespace {

using nlohmann::json;

// Wrappers that carry no structure of their own -- tree-sitter sees through
// them when building the parse table, so inference has to as well.
bool IsWrapper(std::string_view type) {
    return type == "PREC" || type == "PREC_LEFT" || type == "PREC_RIGHT" || type == "PREC_DYNAMIC" ||
           type == "FIELD" || type == "ALIAS";
}


std::string TypeOf(const json& rule) {
    if (!rule.is_object()) return "";
    const auto it = rule.find("type");
    return (it != rule.end() && it->is_string()) ? it->get<std::string>() : "";
}

const json& Unwrap(const json& rule) {
    const json* current = &rule;
    // Bounded rather than while(true): a malformed grammar must not spin here.
    for (int depth = 0; depth < 16; ++depth) {
        if (!IsWrapper(TypeOf(*current))) break;
        const auto content = current->find("content");
        if (content == current->end()) break;
        current = &*content;
    }
    return *current;
}

// A rule wrapped in TOKEN produces a single leaf node: whatever structure is
// written inside it is not in the tree at all. Looking through one is
// therefore backwards -- it finds delimiters in something that has no
// interior to delimit. C's `system_lib_string` (the `<stdio.h>` of an
// #include) is exactly that: a TOKEN whose body happens to read as
// '<' repeat(...) '>'.
bool IsSingleToken(const json& rule) {
    const std::string type = TypeOf(rule);
    return type == "TOKEN" || type == "IMMEDIATE_TOKEN";
}

// A member that may match nothing, and so may legitimately trail a real
// closer. JavaScript's statement_block is SEQ['{', REPEAT(statement), '}',
// <optional>] -- requiring the closer to be the literal last member misses
// it, and misses statement_block in TypeScript and TSX for the same reason.
bool IsOptional(const json& rule) {
    const json&       inner = Unwrap(rule);
    const std::string type  = TypeOf(inner);
    if (type == "REPEAT" || type == "BLANK") return true;
    if (type == "CHOICE") {
        const auto members = inner.find("members");
        if (members == inner.end() || !members->is_array()) return false;
        return std::any_of(members->begin(), members->end(),
                           [](const json& m) { return TypeOf(Unwrap(m)) == "BLANK"; });
    }
    return false;
}

// Every literal this member could match: a STRING, or a CHOICE of them.
// TypeScript's `object_type` opens with CHOICE["{", "{|"] -- one node, two
// spellings -- so treating an opener as a single string misses it.
std::set<std::string> Literals(const json& member) {
    const json&           inner = Unwrap(member);
    std::set<std::string> found;
    const auto            add = [&found](const json& candidate) {
        if (TypeOf(candidate) != "STRING") return;
        const auto value = candidate.find("value");
        if (value != candidate.end() && value->is_string()) found.insert(value->get<std::string>());
    };
    if (TypeOf(inner) == "CHOICE") {
        if (const auto members = inner.find("members"); members != inner.end() && members->is_array()) {
            for (const json& member2 : *members) add(Unwrap(member2));
        }
    }
    else {
        add(inner);
    }
    return found;
}

bool IsSymbolNamed(const json& member, const std::set<std::string>& names) {
    const json& inner = Unwrap(member);
    if (TypeOf(inner) != "SYMBOL") return false;
    const auto name = inner.find("name");
    return name != inner.end() && name->is_string() && names.count(name->get<std::string>()) > 0;
}

// A SEQ's members, with hidden (_-prefixed) rules inlined.
//
// Load-bearing, not a nicety: tree-sitter inlines hidden rules rather than
// making them nodes, so a grammar is free to put a node's real delimiters
// one level down. Clojure does exactly that -- list_lit is
// SEQ[REPEAT(_metadata_lit), _bare_list_lit] with the parens inside
// _bare_list_lit -- and without inlining all six of its fold nodes are
// invisible.
// `indirect` is set when the members came from somewhere other than this
// rule's own top-level sequence -- through a CHOICE branch or a bare hidden
// reference. That matters for indentation bodies specifically: an
// indent-delimited body has no opener, only a closing dedent, so a rule that
// merely *contains* one would otherwise inherit it. Python's class_definition
// ends in a hidden `_suite` that bottoms out at `_dedent`, and reporting the
// whole `class Widget:` header as the body is exactly wrong -- `block`, which
// owns the dedent directly, is the body. Bracket delimiters are safe to
// inherit this way (Kotlin's function_body genuinely is its `_block`); a
// dedent is not.
bool FlattenSeq(const json& rule, const json& rules, std::vector<const json*>& out, int depth,
                std::set<std::string>& seen, bool* indirect = nullptr) {
    const json& inner = Unwrap(rule);

    // A rule may be a CHOICE whose branches include a delimited body: Kotlin's
    // function_body is CHOICE[_block, SEQ["=", expression]] -- a brace body or
    // an expression body. The node is delimited when it takes the first
    // branch, which is exactly why the hand-written query writes
    // `(function_body "{")` rather than a bare capture. Take the first branch
    // that flattens to a sequence; a branch that is a bare token cannot be one.
    if (TypeOf(inner) == "CHOICE" && depth < 4) {
        const auto members = inner.find("members");
        if (members == inner.end() || !members->is_array()) return false;
        for (const json& branch : *members) {
            std::vector<const json*> branchOut;
            std::set<std::string>    branchSeen = seen;
            if (FlattenSeq(branch, rules, branchOut, depth + 1, branchSeen) && branchOut.size() >= 2) {
                out = std::move(branchOut);
                if (indirect != nullptr) *indirect = true;
                return true;
            }
        }
        return false;
    }

    if (TypeOf(inner) != "SEQ") {
        // A bare reference to a hidden rule: Kotlin's control_structure_body
        // branch is just `_block`, whose own production carries the braces.
        if (depth < 4 && TypeOf(inner) == "SYMBOL") {
            const auto name = inner.find("name");
            if (name != inner.end() && name->is_string()) {
                const std::string symbol = name->get<std::string>();
                if (!symbol.empty() && symbol.front() == '_' && !seen.count(symbol)) {
                    if (const auto target = rules.find(symbol); target != rules.end()) {
                        seen.insert(symbol);
                        if (indirect != nullptr) *indirect = true;
                        return FlattenSeq(*target, rules, out, depth + 1, seen, indirect);
                    }
                }
            }
        }
        return false;
    }
    const auto members = inner.find("members");
    if (members == inner.end() || !members->is_array()) return false;

    for (const json& raw : *members) {
        const json& member = Unwrap(raw);
        if (depth < 4 && TypeOf(member) == "SYMBOL") {
            const auto name = member.find("name");
            if (name != member.end() && name->is_string()) {
                const std::string symbol = name->get<std::string>();
                if (!symbol.empty() && symbol.front() == '_' && !seen.count(symbol)) {
                    const auto target = rules.find(symbol);
                    if (target != rules.end()) {
                        seen.insert(symbol);
                        std::vector<const json*> nested;
                        if (FlattenSeq(*target, rules, nested, depth + 1, seen, indirect)) {
                            out.insert(out.end(), nested.begin(), nested.end());
                            continue;
                        }
                    }
                }
            }
        }
        out.push_back(&member);
    }
    return true;
}

// Angle brackets included: a template/type parameter list is a real
// multi-element container that a long declaration wraps across, and a JSX
// opening element is the same shape. Measured cost of adding them is 20 more
// nodes across the bundled grammars, of which the only one that is not a
// genuine container -- C's `system_lib_string` -- is excluded by IsSingleToken
// above rather than by special-casing '<'.
constexpr std::string_view kOpeners = "{([<";
constexpr std::string_view kClosers = "})]>";

std::string OpenerFor(const std::string& closer) {
    const std::size_t index = kClosers.find(closer);
    return index == std::string_view::npos ? "" : std::string(1, kOpeners[index]);
}

// The bracket half of the match, shared by the ordinary path and the
// optional-trailing-body path below it.
std::optional<imprint::DelimitedBody> MatchBracketed(const std::vector<const json*>& core) {
    if (core.size() < 2) return std::nullopt;

    std::string closer;
    for (const std::string& candidate : Literals(*core.back())) {
        if (kClosers.find(candidate) != std::string_view::npos) {
            closer = candidate;
            break;
        }
    }
    if (closer.empty()) return std::nullopt;

    const std::string opener   = OpenerFor(closer);
    const auto        openerIt = std::find_if(core.begin(), core.end() - 1,
                                       [&](const json* m) { return Literals(*m).count(opener) > 0; });
    if (openerIt == core.end() - 1) return std::nullopt;

    imprint::DelimitedBody body;
    body.kind             = imprint::DelimiterKind::Bracket;
    body.openerIsFirst    = (openerIt == core.begin());
    body.listLikeInterior = std::any_of(openerIt + 1, core.end() - 1, [](const json* m) {
        const std::string type = TypeOf(Unwrap(*m));
        return type == "REPEAT" || type == "REPEAT1" || type == "CHOICE";
    });
    return body;
}

} // namespace

std::map<std::string, imprint::DelimitedBody> InferDelimitedBodies(const nlohmann::json& grammar) {
    std::map<std::string, imprint::DelimitedBody> found;

    const auto rules = grammar.find("rules");
    if (rules == grammar.end() || !rules->is_object()) return found;

    std::set<std::string> externals;
    if (const auto list = grammar.find("externals"); list != grammar.end() && list->is_array()) {
        for (const json& entry : *list) {
            if (TypeOf(entry) != "SYMBOL") continue;
            if (const auto name = entry.find("name"); name != entry.end() && name->is_string()) {
                externals.insert(name->get<std::string>());
            }
        }
    }

    for (const auto& [name, rule] : rules->items()) {
        if (!name.empty() && name.front() == '_') continue; // hidden: never a node

        if (IsSingleToken(rule)) continue; // a leaf, whatever it looks like inside

        std::vector<const json*> members;
        std::set<std::string>    seen;
        bool                     indirect = false;
        if (!FlattenSeq(rule, *rules, members, 0, seen, &indirect) || members.size() < 2) continue;

        // Trim trailing optionals to find the real closer, but never trim
        // away the whole production.
        std::vector<const json*> core = members;
        while (core.size() > 1 && IsOptional(*core.back())) core.pop_back();
        if (core.size() < 2) core = members;

        if (const auto bracket = MatchBracketed(core); bracket.has_value()) {
            found.emplace(name, *bracket);
            continue;
        }

        // An OPTIONAL trailing body. Kotlin's secondary_constructor is
        // SEQ[..., CHOICE[_block, BLANK]] -- `constructor(...) { ... }` or
        // `constructor(...)` with nothing at all -- so the trailing-optional
        // trim that finds the closer for every other shape removes the very
        // member that delimits this one. The hand-written query writes
        // `(secondary_constructor "{")` for exactly this reason.
        //
        // Handled rather than declined: the shape is not Kotlin's alone (an
        // optional brace body is how `= default`, `= delete` and abstract
        // declarations are spelled elsewhere), and a rule that is right only
        // for the grammars already looked at is not a rule.
        if (!members.empty() && IsOptional(*members.back())) {
            std::vector<const json*> inner;
            std::set<std::string>    innerSeen;
            bool                     innerIndirect = false;
            if (FlattenSeq(*members.back(), *rules, inner, 1, innerSeen, &innerIndirect)) {
                if (auto bracket = MatchBracketed(inner); bracket.has_value()) {
                    // The opener cannot be this node's own first member -- the
                    // header that made the body optional precedes it.
                    bracket->openerIsFirst = false;
                    found.emplace(name, *bracket);
                    continue;
                }
            }
        }

        if (!indirect && IsSymbolNamed(*core.back(), externals)) {
            imprint::DelimitedBody body;
            body.kind = imprint::DelimiterKind::Indent;
            // An indentation body has no opener of its own, and everything
            // before the closing dedent is its content -- which is a
            // statement list by construction.
            body.openerIsFirst    = true;
            body.listLikeInterior = true;
            found.emplace(name, body);
        }
    }

    return found;
}

} // namespace ned::editor::treesitter
