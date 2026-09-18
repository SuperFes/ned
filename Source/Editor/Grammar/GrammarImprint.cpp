#include "GrammarImprint.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace ned::editor::grammar {

namespace {

    using Rule    = compile::GrammarFile::Rule;
    using Kind    = Rule::Kind;
    using RuleMap = std::map<std::string, const Rule*>;

    // Wrappers that carry no structure of their own -- the parser sees through
    // them when building the parse table, so inference has to as well.
    bool IsWrapper(Kind kind) {
        return kind == Kind::Prec || kind == Kind::PrecLeft || kind == Kind::PrecRight || kind == Kind::PrecDynamic || kind == Kind::Field ||
               kind == Kind::Alias;
    }

    const Rule& Unwrap(const Rule& rule) {
        const Rule* current = &rule;
        // Bounded rather than while(true): a malformed grammar must not spin here.
        for (int depth = 0; depth < 16; ++depth) {
            if (!IsWrapper(current->kind) || current->children.empty())
                break;
            current = &current->Child();
        }
        return *current;
    }

    // A rule wrapped in TOKEN produces a single leaf node: whatever structure is
    // written inside it is not in the tree at all. Looking through one is
    // therefore backwards -- it finds delimiters in something that has no
    // interior to delimit. C's `system_lib_string` (the `<stdio.h>` of an
    // #include) is exactly that: a TOKEN whose body happens to read as
    // '<' repeat(...) '>'.
    bool IsSingleToken(const Rule& rule) {
        return rule.kind == Kind::Token || rule.kind == Kind::TokenImmediate;
    }

    // A member that may match nothing, and so may legitimately trail a real
    // closer. JavaScript's statement_block is SEQ['{', REPEAT(statement), '}',
    // <optional>] -- requiring the closer to be the literal last member misses
    // it, and misses statement_block in TypeScript and TSX for the same reason.
    bool IsOptional(const Rule& rule) {
        const Rule& inner = Unwrap(rule);
        if (inner.kind == Kind::Repeat || inner.kind == Kind::Blank)
            return true;
        if (inner.kind == Kind::Choice)
            return std::any_of(inner.children.begin(), inner.children.end(), [](const Rule& m) { return Unwrap(m).kind == Kind::Blank; });
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
    const Rule& UnwrapIncludingTokens(const Rule& rule) {
        const Rule* current = &Unwrap(rule);
        for (int depth = 0; depth < 16; ++depth) {
            if (!IsSingleToken(*current) || current->children.empty())
                break;
            current = &Unwrap(current->Child());
        }
        return *current;
    }

    // An UNNAMED alias is emitted as an anonymous token spelled `value`, whatever
    // it wraps -- Rust's string_literal opens with alias(/[bc]?"/, '"'), and the
    // tree carries a plain `"` there, indistinguishable from a literal. Unwrap()
    // sees through the alias to the PATTERN and loses that; it has to be asked
    // before unwrapping. (A NAMED alias is a node, which is CollectAliasedNodes'
    // business, not a literal's.)
    std::optional<std::string> UnnamedAliasValue(const Rule& member) {
        const Rule* current = &member;
        for (int depth = 0; depth < 16; ++depth) {
            if (current->kind == Kind::Alias && !current->named)
                return current->text;
            if ((!IsWrapper(current->kind) && !IsSingleToken(*current)) || current->children.empty())
                break;
            current = &current->Child();
        }
        return std::nullopt;
    }

    std::set<std::string> Literals(const Rule& member) {
        std::set<std::string> found;
        const auto            add = [&found](const Rule& raw) {
            if (const auto alias = UnnamedAliasValue(raw); alias.has_value()) {
                found.insert(*alias);
                return;
            }
            const Rule& candidate = UnwrapIncludingTokens(raw);
            if (candidate.kind == Kind::String)
                found.insert(candidate.text);
        };
        const Rule& inner = UnwrapIncludingTokens(member);
        if (inner.kind == Kind::Choice) {
            for (const Rule& member2 : inner.children)
                add(member2);
        }
        else {
            add(member);
        }
        return found;
    }

    bool IsSymbolNamed(const Rule& member, const std::set<std::string>& names) {
        const Rule& inner = Unwrap(member);
        return inner.kind == Kind::Symbol && names.count(inner.text) > 0;
    }

    bool IsListLike(const Rule& member) {
        const Kind kind = Unwrap(member).kind;
        return kind == Kind::Repeat || kind == Kind::Repeat1 || kind == Kind::Choice;
    }

    // A SEQ's members, with hidden (_-prefixed) rules inlined.
    //
    // Load-bearing, not a nicety: the parser inlines hidden rules rather than
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
    bool FlattenSeq(const Rule& rule, const RuleMap& rules, std::vector<const Rule*>& out, int depth, std::set<std::string>& seen,
                    bool* indirect = nullptr) {
        const Rule& inner = Unwrap(rule);

        // A rule may be a CHOICE whose branches include a delimited body: Kotlin's
        // function_body is CHOICE[_block, SEQ["=", expression]] -- a brace body or
        // an expression body. The node is delimited when it takes the first
        // branch, which is exactly why the hand-written query writes
        // `(function_body "{")` rather than a bare capture. Take the first branch
        // that flattens to a sequence; a branch that is a bare token cannot be one.
        if (inner.kind == Kind::Choice && depth < 4) {
            for (const Rule& branch : inner.children) {
                std::vector<const Rule*> branchOut;
                std::set<std::string>    branchSeen = seen;
                if (FlattenSeq(branch, rules, branchOut, depth + 1, branchSeen) && branchOut.size() >= 2) {
                    out = std::move(branchOut);
                    if (indirect != nullptr)
                        *indirect = true;
                    return true;
                }
            }
            return false;
        }

        if (inner.kind != Kind::Seq) {
            // A bare reference to a hidden rule: Kotlin's control_structure_body
            // branch is just `_block`, whose own production carries the braces.
            if (depth < 4 && inner.kind == Kind::Symbol) {
                const std::string& symbol = inner.text;
                if (!symbol.empty() && symbol.front() == '_' && !seen.count(symbol)) {
                    if (const auto target = rules.find(symbol); target != rules.end()) {
                        seen.insert(symbol);
                        if (indirect != nullptr)
                            *indirect = true;
                        return FlattenSeq(*target->second, rules, out, depth + 1, seen, indirect);
                    }
                }
            }
            return false;
        }

        for (const Rule& raw : inner.children) {
            const Rule& member = Unwrap(raw);
            if (depth < 4 && member.kind == Kind::Symbol) {
                const std::string& symbol = member.text;
                if (!symbol.empty() && symbol.front() == '_' && !seen.count(symbol)) {
                    if (const auto target = rules.find(symbol); target != rules.end()) {
                        seen.insert(symbol);
                        std::vector<const Rule*> nested;
                        bool                     nestedIndirect = false;
                        if (FlattenSeq(*target->second, rules, nested, depth + 1, seen, &nestedIndirect)) {
                            out.insert(out.end(), nested.begin(), nested.end());
                            // Only the tail matters: this inline is the last
                            // member *so far*, and a later direct member will
                            // clear it again below.
                            if (indirect != nullptr)
                                *indirect = true;
                            continue;
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
            if (indirect != nullptr)
                *indirect = false;
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

    // Every named alias in the grammar, mapped to the production it renames.
    // An ALIAS with `named: true` creates a visible node of that name wrapping
    // its content -- which is how a grammar whose rules are all hidden still
    // produces a readable tree.
    //
    // The walk is bounded, and counted the way the grammar.json object tree
    // nests (a member list is one level, each member another): the first alias
    // site found for a name is the one analysed, and both the bound and the
    // rule order (alphabetical, see the caller) are part of what the measured
    // tables pinned.
    void CollectAliasedNodes(const Rule& node, const RuleMap& rules, std::map<std::string, const Rule*>& out, int depth) {
        if (depth > 24)
            return;
        if (node.kind == Kind::Alias && node.named && !node.children.empty()) {
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
            const Rule* target = &node.Child();
            if (target->kind == Kind::Symbol) {
                if (const auto rule = rules.find(target->text); rule != rules.end())
                    target = rule->second;
            }
            out.emplace(node.text, target);
        }
        const int step = (node.kind == Kind::Seq || node.kind == Kind::Choice) ? 2 : 1;
        for (const Rule& child : node.children)
            CollectAliasedNodes(child, rules, out, depth + step);
    }

    // A keyword: letters and underscores only, so `fi`, `done`, `end`, `esac`
    // qualify and `;`, `)`, `=>` do not. What makes a pair is being two DISTINCT
    // keywords at the two ends of one production with a list between them --
    // `do ... done`, `if ... fi`, fish's `function ... end`. Measured across all
    // 23 bundled grammars before shipping; see Docs/ParsingEngine.md for the
    // false-positive audit.
    bool IsKeyword(std::string_view literal) {
        return !literal.empty() && std::all_of(literal.begin(), literal.end(), [](unsigned char c) { return std::isalpha(c) != 0 || c == '_'; });
    }

    std::optional<imprint::DelimitedBody> MatchKeywordPair(const std::vector<const Rule*>& core) {
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
        const bool listLike = std::any_of(core.begin() + 1, core.end() - 1, [](const Rule* m) { return IsListLike(*m); });
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

    // The closer need not be the production's own final member. `if_statement`/
    // `while_statement`/`switch_statement`'s "(condition)" factors out into a
    // shared condition_clause rule that legitimately ends in ')', but
    // `for_statement`/`for_range_loop` write their own "(...)" inline and follow
    // it with a REQUIRED trailing `body` field (the loop's statement/block) --
    // SEQ['for', '(', ..., ')', FIELD:body]. The old "closer is core.back()"
    // check skipped both entirely, so a multi-line for-loop header got no
    // indent/fold/bracket-match contribution at all: a continuation line landed
    // flush with the `for` itself instead of aligned or indented one level,
    // reported live against this project's own main.cpp.
    //
    // Searching backward for the LAST member naming a real closing-bracket
    // literal, rather than assuming index size()-1, finds it -- but ONLY
    // members that are themselves a genuinely named FIELD (checked on the RAW,
    // pre-Unwrap member `core` itself stores, since Unwrap/Literals sees
    // through a FIELD wrapper to its content) may be skipped on the way there.
    // That restriction is load-bearing, not incidental: TOML's `table` trails
    // its own closing ']' with a bare, UNNAMED external-scanner symbol
    // (`_line_ending_or_eof`, a required line terminator) before its real
    // list-like content (a REPEAT of pairs, already trimmed by the existing
    // IsOptional loop above this function). That trailing symbol has no
    // literal either -- the exact same shape a real trailing body field has --
    // so an unrestricted backward search misclassified `table` as a Bracket
    // body wrapping content it does not actually delimit (a table's body is
    // unbounded, closed only by the next header or EOF, i.e. genuinely
    // DelimiterKind::Indent). Requiring FIELD stops the search there instead:
    // `_line_ending_or_eof` is a bare SYMBOL, not a FIELD, so the walk halts
    // without finding a closer, preserving `table`'s original classification.
    // Every production whose closer already sits at the last index (the common
    // case) resolves identically to before either way.
    std::optional<imprint::DelimitedBody> MatchBracketed(const std::vector<const Rule*>& core) {
        if (core.size() < 2)
            return std::nullopt;

        std::size_t closerIndex = core.size();
        std::string closer;
        for (std::size_t i = core.size(); i-- > 0;) {
            for (const std::string& candidate : Literals(*core[i])) {
                if (kClosers.find(candidate) != std::string_view::npos) {
                    closer      = candidate;
                    closerIndex = i;
                    break;
                }
            }
            if (!closer.empty())
                break;
            if (core[i]->kind != Kind::Field)
                break; // not a closer, and not safe to skip past either -- stop searching
        }
        if (closer.empty() || closerIndex == 0)
            return std::nullopt; // need at least one member before it to open

        const std::string opener   = OpenerFor(closer);
        const auto        closerIt = core.begin() + static_cast<std::ptrdiff_t>(closerIndex);
        const auto        openerIt = std::find_if(core.begin(), closerIt, [&](const Rule* m) {
            const std::set<std::string> literals = Literals(*m);
            return std::any_of(literals.begin(), literals.end(), [&](const std::string& literal) { return imprint::OpensWithBracket(literal, opener[0]); });
        });
        if (openerIt == closerIt)
            return std::nullopt;

        imprint::DelimitedBody body;
        body.kind             = imprint::DelimiterKind::Bracket;
        body.openerIsFirst    = (openerIt == core.begin());
        body.listLikeInterior = std::any_of(openerIt + 1, closerIt, [](const Rule* m) { return IsListLike(*m); });
        return body;
    }

} // namespace

std::map<std::string, imprint::DelimitedBody> InferDelimitedBodies(const compile::GrammarFile& grammar) {
    std::map<std::string, imprint::DelimitedBody> found;

    RuleMap rules;
    for (const auto& [name, rule] : grammar.rules)
        rules.emplace(name, &rule);

    std::set<std::string> externals;
    for (const Rule& entry : grammar.externals)
        if (entry.kind == Kind::Symbol)
            externals.insert(entry.text);

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
    // Rules are visited by name, not in grammar order: which alias site is
    // found first for a name decides its production.
    std::map<std::string, const Rule*> aliased;
    for (const auto& [name, rule] : rules)
        CollectAliasedNodes(*rule, rules, aliased, 1);

    std::vector<std::pair<std::string, const Rule*>> candidates;
    candidates.reserve(rules.size() + aliased.size());
    for (const auto& [name, rule] : rules)
        candidates.emplace_back(name, rule);
    for (const auto& [name, content] : aliased)
        if (rules.count(name) == 0)
            candidates.emplace_back(name, content);

    for (const auto& [name, rulePtr] : candidates) {
        const Rule& rule = *rulePtr;
        if (!name.empty() && name.front() == '_')
            continue; // hidden: never a node

        if (IsSingleToken(rule))
            continue; // a leaf, whatever it looks like inside

        std::vector<const Rule*> members;
        std::set<std::string>    seen;
        bool                     indirect = false;
        if (!FlattenSeq(rule, rules, members, 0, seen, &indirect) || members.size() < 2)
            continue;

        // Trim trailing optionals to find the real closer, but never trim
        // away the whole production.
        std::vector<const Rule*> core = members;
        while (core.size() > 1 && IsOptional(*core.back()))
            core.pop_back();
        if (core.size() < 2)
            core = members;

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
            std::vector<const Rule*> inner;
            std::set<std::string>    innerSeen;
            bool                     innerIndirect = false;
            if (FlattenSeq(*members.back(), rules, inner, 1, innerSeen, &innerIndirect)) {
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

} // namespace ned::editor::grammar
