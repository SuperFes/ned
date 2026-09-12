#include "GrammarImprint.h"

#include <algorithm>
#include <cctype>
#include <map>
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
// Reading a literal looks through TOKEN as well, which IsSingleToken above
// deliberately does not. The two are asking different questions: a rule whose
// WHOLE production is a TOKEN is a leaf node with no interior, while a TOKEN
// sitting inside a sequence is just a token carrying a precedence -- Bash
// writes its compound_statement's closing brace that way, and refusing to read
// it lost every Bash fold.
const json& UnwrapIncludingTokens(const json& rule) {
    const json* current = &Unwrap(rule);
    for (int depth = 0; depth < 16; ++depth) {
        if (!IsSingleToken(*current)) break;
        const auto content = current->find("content");
        if (content == current->end()) break;
        current = &Unwrap(*content);
    }
    return *current;
}

// An UNNAMED alias is emitted as an anonymous token spelled `value`, whatever
// it wraps -- Rust's string_literal opens with alias(/[bc]?"/, '"'), and the
// tree carries a plain `"` there, indistinguishable from a literal. Unwrap()
// sees through the alias to the PATTERN and loses that; it has to be asked
// before unwrapping. (A NAMED alias is a node, which is CollectAliasedNodes'
// business, not a literal's.)
std::optional<std::string> UnnamedAliasValue(const json& member) {
    const json* current = &member;
    for (int depth = 0; depth < 16; ++depth) {
        if (TypeOf(*current) == "ALIAS") {
            const auto named = current->find("named");
            const auto value = current->find("value");
            if (named != current->end() && named->is_boolean() && !named->get<bool>() && value != current->end() &&
                value->is_string()) {
                return value->get<std::string>();
            }
        }
        if (!IsWrapper(TypeOf(*current)) && !IsSingleToken(*current))
            break;
        const auto content = current->find("content");
        if (content == current->end())
            break;
        current = &*content;
    }
    return std::nullopt;
}

std::set<std::string> Literals(const json& member) {
    std::set<std::string> found;
    const auto            add = [&found](const json& raw) {
        if (const auto alias = UnnamedAliasValue(raw); alias.has_value()) {
            found.insert(*alias);
            return;
        }
        const json& candidate = UnwrapIncludingTokens(raw);
        if (TypeOf(candidate) != "STRING")
            return;
        const auto value = candidate.find("value");
        if (value != candidate.end() && value->is_string())
            found.insert(value->get<std::string>());
    };
    const json& inner = UnwrapIncludingTokens(member);
    if (TypeOf(inner) == "CHOICE") {
        if (const auto members = inner.find("members"); members != inner.end() && members->is_array()) {
            for (const json& member2 : *members)
                add(member2);
        }
    }
    else {
        add(member);
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
// `indirect` reports whether the FINAL member -- the one that closes the
// production -- was reached by inlining rather than written in this rule's own
// sequence. That is the question worth asking, and asking it loosely was a bug
// worth recording.
//
// An indent-delimited body has no opener, only a closing dedent, so a rule
// that merely *contains* one must not inherit it: Python's class_definition
// ends in a hidden `_suite` that bottoms out at `_dedent`, and reporting the
// whole `class Widget:` header as the body is exactly wrong -- `block`, which
// owns the dedent directly, is the body.
//
// But the first version set this for inlining ANYWHERE in the sequence, which
// silently disqualified rules whose closer is their own. YAML's block_mapping
// is SEQ[_r_blk_map_itm, REPEAT(...), _bl] -- `_bl` is an external and is
// written right there, while the inlining happened on an earlier member -- so
// every YAML block was invisible and the language looked like it had nothing
// foldable but the whole document. Bracket delimiters are safe to inherit
// either way (Kotlin's function_body genuinely is its `_block`); only the
// dedent case needs this, and only about its last member.
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
                        bool                     nestedIndirect = false;
                        if (FlattenSeq(*target, rules, nested, depth + 1, seen, &nestedIndirect)) {
                            out.insert(out.end(), nested.begin(), nested.end());
                            // Only the tail matters: this inline is the last
                            // member *so far*, and a later direct member will
                            // clear it again below.
                            if (indirect != nullptr) *indirect = true;
                            continue;
                        }
                    }
                }
            }
        }
        // The raw member, wrappers and all: every reader unwraps for itself,
        // and Literals needs to see an unnamed ALIAS before it is stripped.
        out.push_back(&raw);
        // A member written directly in this sequence becomes the tail, so
        // whatever indirection an earlier member involved no longer describes
        // the closer.
        if (indirect != nullptr) *indirect = false;
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
// Every named alias in the grammar, mapped to the production it renames.
// An ALIAS with `named: true` and a string `value` creates a visible node of
// that name wrapping `content` -- which is how a grammar whose rules are all
// hidden still produces a readable tree.
void CollectAliasedNodes(const json& node, const json& rules, std::map<std::string, const json*>& out,
                         int depth) {
    if (depth > 24) return;
    if (node.is_object()) {
        if (TypeOf(node) == "ALIAS") {
            const auto named = node.find("named");
            const auto value = node.find("value");
            const auto content = node.find("content");
            if (named != node.end() && named->is_boolean() && named->get<bool>() && value != node.end() &&
                value->is_string() && content != node.end()) {
                // The content is almost always a SYMBOL naming the hidden rule
                // being renamed -- alias($._block_mapping, 'block_mapping').
                // Resolve it, so what gets analysed is the real production
                // rather than a bare reference to it.
                //
                // Resolving here rather than letting FlattenSeq chase the
                // symbol is not a shortcut: an alias is a RENAME, not
                // containment, so it must not be treated as indirection. The
                // indirect flag exists to stop a rule inheriting a dedent from
                // a body it merely contains (Python's class_definition through
                // _suite); an aliased rule IS the body, under another name, and
                // blocking it there is what kept every YAML block invisible.
                const json* target = &*content;
                if (TypeOf(*target) == "SYMBOL") {
                    if (const auto symbol = target->find("name");
                        symbol != target->end() && symbol->is_string()) {
                        if (const auto rule = rules.find(symbol->get<std::string>()); rule != rules.end()) {
                            target = &*rule;
                        }
                    }
                }
                out.emplace(value->get<std::string>(), target);
            }
        }
        for (const auto& [key, child] : node.items()) CollectAliasedNodes(child, rules, out, depth + 1);
        return;
    }
    if (node.is_array()) {
        for (const json& child : node) CollectAliasedNodes(child, rules, out, depth + 1);
    }
}

// A keyword: letters and underscores only, so `fi`, `done`, `end`, `esac`
// qualify and `;`, `)`, `=>` do not. What makes a pair is being two DISTINCT
// keywords at the two ends of one production with a list between them --
// `do ... done`, `if ... fi`, fish's `function ... end`. Measured across all
// 23 bundled grammars before shipping; see Docs/ParsingEngine.md for the
// false-positive audit.
bool IsKeyword(std::string_view literal) {
    return !literal.empty() && std::all_of(literal.begin(), literal.end(), [](unsigned char c) {
        return std::isalpha(c) != 0 || c == '_';
    });
}

std::optional<imprint::DelimitedBody> MatchKeywordPair(const std::vector<const json*>& core) {
    if (core.size() < 3)
        return std::nullopt; // opener, something, closer -- two keywords alone is a phrase
    const std::set<std::string> closers = Literals(*core.back());
    const std::set<std::string> openers = Literals(*core.front());
    if (closers.size() != 1 || openers.size() != 1)
        return std::nullopt;
    const std::string& closer = *closers.begin();
    const std::string& opener = *openers.begin();
    if (!IsKeyword(closer) || !IsKeyword(opener) || closer == opener)
        return std::nullopt;

    // A pair around a single thing is a phrase, not a body: bash's
    // `elif ... then` (the condition sits between them; the commands come
    // after) and JavaScript's `new ... target`. Measured: those two are the
    // only non-list-like pairs in all 23 grammars, and the nine list-like
    // ones are exactly the nine bash/fish wrote by hand.
    const bool listLike = std::any_of(core.begin() + 1, core.end() - 1, [](const json* m) {
        const std::string type = TypeOf(Unwrap(*m));
        return type == "REPEAT" || type == "REPEAT1" || type == "CHOICE";
    });
    if (!listLike)
        return std::nullopt;

    imprint::DelimitedBody body;
    body.kind             = imprint::DelimiterKind::Keyword;
    body.openerIsFirst    = true;
    body.listLikeInterior = true;
    body.opener           = opener;
    body.closer           = closer;
    return body;
}

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
    const auto        openerIt = std::find_if(core.begin(), core.end() - 1, [&](const json* m) {
        const std::set<std::string> literals = Literals(*m);
        return std::any_of(literals.begin(), literals.end(),
                           [&](const std::string& literal) { return imprint::OpensWithBracket(literal, opener[0]); });
    });
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

    // A grammar may name its nodes with alias() rather than by rule name, and
    // then every rule can be hidden. tree-sitter-yaml does exactly that: all
    // 202 of its rules are `_`-prefixed, and `block_mapping`, `block_node` and
    // `block_sequence` -- the nodes a reader actually folds -- exist only as
    // ALIAS values. Unwrapping ALIAS (which Unwrap does, correctly, when
    // looking at structure) sees through the very thing that creates the node,
    // so those were invisible and YAML looked like it had nothing foldable but
    // the whole document.
    //
    // Same mistake shape as TOKEN above: seeing through something that is not
    // merely a wrapper. Collected first so an alias-named body is analysed
    // like any other.
    std::map<std::string, const json*> aliased;
    CollectAliasedNodes(*rules, *rules, aliased, 0);

    std::vector<std::pair<std::string, const json*>> candidates;
    candidates.reserve(rules->size() + aliased.size());
    for (const auto& [name, rule] : rules->items()) candidates.emplace_back(name, &rule);
    for (const auto& [name, content] : aliased) {
        if (!rules->contains(name)) candidates.emplace_back(name, content);
    }

    for (const auto& [name, rulePtr] : candidates) {
        const json& rule = *rulePtr;
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
        if (const auto keyword = MatchKeywordPair(core); keyword.has_value()) {
            found.emplace(name, *keyword);
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
            // Closing with a scanner token covers two genuinely different
            // shapes, and folding needs them apart. Python's `block` is
            // SEQ[REPEAT(_statement), _dedent] -- pure content, no introducer
            // of its own, so the line that names it (`def f():`, `if x:`) is
            // its PARENT's. Python's `if_statement` closes the same way but
            // opens with the literal `if`, and TOML's `table` with `[`: those
            // carry their own header on their own first line.
            //
            // An external counts as an introducer too -- Python's `string` is
            // SEQ[string_start, ..., string_end], and string_start is the
            // opening quote however the scanner spells it.
            body.openerIsFirst    = !Literals(*core.front()).empty() || IsSymbolNamed(*core.front(), externals);
            body.listLikeInterior = true;
            found.emplace(name, body);
        }
    }

    return found;
}

} // namespace ned::editor::treesitter
