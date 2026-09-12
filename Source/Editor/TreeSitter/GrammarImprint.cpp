#include "GrammarImprint.h"

#include <algorithm>
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
           type == "FIELD" || type == "ALIAS" || type == "TOKEN" || type == "IMMEDIATE_TOKEN";
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

std::string Literal(const json& member) {
    const json& inner = Unwrap(member);
    if (TypeOf(inner) != "STRING") return "";
    const auto value = inner.find("value");
    return (value != inner.end() && value->is_string()) ? value->get<std::string>() : "";
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
bool FlattenSeq(const json& rule, const json& rules, std::vector<const json*>& out, int depth,
                std::set<std::string>& seen) {
    const json& inner = Unwrap(rule);
    if (TypeOf(inner) != "SEQ") return false;
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
                        if (FlattenSeq(*target, rules, nested, depth + 1, seen)) {
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

constexpr std::string_view kOpeners = "{([";
constexpr std::string_view kClosers = "})]";

std::string OpenerFor(const std::string& closer) {
    const std::size_t index = kClosers.find(closer);
    return index == std::string_view::npos ? "" : std::string(1, kOpeners[index]);
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

        std::vector<const json*> members;
        std::set<std::string>    seen;
        if (!FlattenSeq(rule, *rules, members, 0, seen) || members.size() < 2) continue;

        // Trim trailing optionals to find the real closer, but never trim
        // away the whole production.
        std::vector<const json*> core = members;
        while (core.size() > 1 && IsOptional(*core.back())) core.pop_back();
        if (core.size() < 2) core = members;

        const std::string closer = Literal(*core.back());
        if (!closer.empty() && kClosers.find(closer) != std::string_view::npos) {
            const std::string opener      = OpenerFor(closer);
            const auto        openerIt    = std::find_if(core.begin(), core.end() - 1,
                                                  [&](const json* m) { return Literal(*m) == opener; });
            if (openerIt != core.end() - 1) {
                imprint::DelimitedBody body;
                body.kind          = imprint::DelimiterKind::Bracket;
                body.openerIsFirst = (openerIt == core.begin());
                body.listLikeInterior =
                    std::any_of(openerIt + 1, core.end() - 1, [](const json* m) {
                        const std::string type = TypeOf(Unwrap(*m));
                        return type == "REPEAT" || type == "REPEAT1" || type == "CHOICE";
                    });
                found.emplace(name, body);
                continue;
            }
        }

        if (IsSymbolNamed(*core.back(), externals)) {
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
