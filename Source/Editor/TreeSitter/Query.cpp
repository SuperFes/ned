#include "Query.h"

#include <optional>
#include <regex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ned::editor::treesitter {

namespace {

    std::string_view QueryErrorName(TSQueryError error) {
        switch (error) {
            case TSQueryErrorNone:
                return "none";
            case TSQueryErrorSyntax:
                return "syntax";
            case TSQueryErrorNodeType:
                return "unknown node type";
            case TSQueryErrorField:
                return "unknown field name";
            case TSQueryErrorCapture:
                return "unknown capture name";
            case TSQueryErrorStructure:
                return "impossible pattern structure";
            case TSQueryErrorLanguage:
                return "language version mismatch";
        }
        return "unknown";
    }

    // The node a given match actually bound to capture index captureId, or a
    // null TSNode if that capture never fired in this match (possible for a
    // capture inside an optional/alternation branch of the pattern).
    TSNode CapturedNode(const TSQueryMatch& match, uint32_t captureId) {
        for (uint16_t i = 0; i < match.capture_count; ++i) {
            if (match.captures[i].index == captureId) {
                return match.captures[i].node;
            }
        }
        return TSNode{};
    }

    // A predicate operand is either a literal string (a String step) or
    // whatever text a capture actually matched (a Capture step) -- #eq?/
    // #match?/#any-of? all just compare text either way, so both resolve to
    // the same std::string_view. nullopt for a capture that never fired --
    // deliberately not treated as an empty string, so a predicate involving
    // it can choose to no-op rather than false-compare against "".
    std::optional<std::string_view> ResolveTextOperand(const TSQuery* query, const TSQueryMatch& match,
                                                       const TSQueryPredicateStep& step,
                                                       std::string_view            sourceText) {
        if (step.type == TSQueryPredicateStepTypeString) {
            uint32_t    length = 0;
            const char* value  = ts_query_string_value_for_id(query, step.value_id, &length);
            return std::string_view(value, length);
        }
        const TSNode node = CapturedNode(match, step.value_id);
        if (ts_node_is_null(node)) {
            return std::nullopt;
        }
        const uint32_t start = ts_node_start_byte(node);
        const uint32_t end   = ts_node_end_byte(node);
        if (start > end || end > sourceText.size()) {
            return std::nullopt; // defensive -- a stale/mismatched sourceText should never crash a repaint
        }
        return sourceText.substr(start, end - start);
    }

    // Lua's %-prefixed character classes (%u uppercase, %l lowercase, %d
    // digit, %a letter, %s whitespace, %w alnum, %p punctuation) have no
    // ECMAScript equivalent syntax -- translated to the nearest bracket-
    // expression form here for the handful of real query files (confirmed
    // in the vendored nvim-treesitter cpp query: "^%u"/"^[%u]", used for
    // constructor/type-name uppercase-first detection) that actually use
    // one. Handles both "[%u]" (already inside a bracket expression) and
    // bare "%u" (needs one added) without double-bracketing -- the
    // bracketed form is substituted first, so a bare-form pass afterward
    // can never re-match what it already consumed. Every other Lua pattern
    // construct (character sets beyond these, anchors within a class,
    // non-greedy captures, ...) is deliberately not translated -- falls
    // through to std::regex throwing on whatever's left, caught by the
    // caller and treated as "don't block on it," same as any other
    // predicate this doesn't fully understand.
    std::string TranslateLuaPatternClasses(std::string pattern) {
        static constexpr std::pair<std::string_view, std::string_view> kClasses[] = {
            {"%u", "A-Z"},
            {"%l", "a-z"},
            {"%d", "0-9"},
            {"%a", "A-Za-z"},
            {"%s", " \\t\\n\\r\\f\\v"},
            {"%w", "A-Za-z0-9"},
            {"%p", "!-/:-@\\[-`{-~"},
        };
        for (const auto& [luaClass, body] : kClasses) {
            const std::string bracketed        = "[" + std::string(luaClass) + "]";
            const std::string bracketedReplace = "[" + std::string(body) + "]";
            for (std::size_t pos = 0; (pos = pattern.find(bracketed, pos)) != std::string::npos;) {
                pattern.replace(pos, bracketed.size(), bracketedReplace);
                pos += bracketedReplace.size();
            }
            const std::string bareReplace = "[" + std::string(body) + "]";
            for (std::size_t pos = 0; (pos = pattern.find(luaClass, pos)) != std::string::npos;) {
                pattern.replace(pos, luaClass.size(), bareReplace);
                pos += bareReplace.size();
            }
        }
        return pattern;
    }

    bool NodeHasAncestorOfType(TSNode node, std::string_view typeName, bool immediateOnly) {
        for (TSNode current = ts_node_parent(node); !ts_node_is_null(current); current = ts_node_parent(current)) {
            if (ts_node_type(current) == typeName) {
                return true;
            }
            if (immediateOnly) {
                return false;
            }
        }
        return false;
    }

    // Evaluates one already-split-out "#name? operand..." predicate call
    // against a specific match. True both when the predicate genuinely
    // passes AND when it's a predicate this doesn't recognize (including
    // #set!, a non-filtering directive real query files use for match
    // priority) -- an unrecognized predicate must never suppress a match;
    // see Query.h's own header comment for why that's the only safe
    // default (Captures() evaluated zero predicates before this existed at
    // all, so "include it" is the pre-existing behavior for anything not
    // explicitly handled here, not a new risk).
    bool EvaluateOnePredicate(const TSQuery* query, const TSQueryMatch& match, std::string_view predicateName,
                              const std::vector<TSQueryPredicateStep>& operands, std::string_view sourceText,
                              std::unordered_map<std::string, std::regex>& regexCache) {
        if (!predicateName.empty() && predicateName.front() == '#') {
            predicateName.remove_prefix(1); // tolerate either spelling -- not assumed which one ts_query_string_value_for_id returns
        }
        const bool             negated  = predicateName.starts_with("not-");
        const std::string_view baseName = negated ? predicateName.substr(4) : predicateName;

        if (baseName == "eq?") {
            if (operands.size() != 2) {
                return true;
            }
            const auto a = ResolveTextOperand(query, match, operands[0], sourceText);
            const auto b = ResolveTextOperand(query, match, operands[1], sourceText);
            if (!a || !b) {
                return true;
            }
            return negated ? (*a != *b) : (*a == *b);
        }

        if (baseName == "match?" || baseName == "lua-match?") {
            if (operands.size() != 2) {
                return true;
            }
            const auto text    = ResolveTextOperand(query, match, operands[0], sourceText);
            const auto pattern = ResolveTextOperand(query, match, operands[1], sourceText);
            if (!text || !pattern) {
                return true;
            }
            try {
                // Cached by translated pattern text (cmake-highlighting-perf
                // follow-up) -- std::regex construction is slow enough that
                // recompiling the same handful of patterns once per matching
                // node (hundreds of times over, for a pattern-heavy query
                // like tree-sitter-cmake's) was a real, measured multi-second
                // first-paint stall, not a theoretical one. Keyed on the
                // already-Lua-translated string so a pattern appearing in
                // more than one predicate (common -- many #match? calls
                // share the same "^[fF][uU]..." case-insensitive spelling
                // idiom) only ever gets compiled once, regardless of how
                // many distinct predicate call sites use it.
                std::string translated = TranslateLuaPatternClasses(std::string(*pattern));
                auto        cacheIt    = regexCache.find(translated);
                if (cacheIt == regexCache.end()) {
                    // ECMAScript, not a Lua-pattern engine -- matches this
                    // project's own existing QueryReplace.h precedent; the
                    // patterns real query files actually use for this
                    // (anchored character classes like "^[A-Z][A-Z0-9_]*$",
                    // or Lua's own %u-style classes via
                    // TranslateLuaPatternClasses) translate directly.
                    std::regex compiled(translated, std::regex::ECMAScript);
                    cacheIt = regexCache.emplace(std::move(translated), std::move(compiled)).first;
                }
                const bool matched = std::regex_search(text->begin(), text->end(), cacheIt->second);
                return negated ? !matched : matched;
            }
            catch (const std::regex_error&) {
                return true; // a Lua-only pattern construct std::regex can't parse -- don't block on it
            }
        }

        if (baseName == "any-of?") {
            if (operands.empty()) {
                return true;
            }
            const auto text = ResolveTextOperand(query, match, operands[0], sourceText);
            if (!text) {
                return true;
            }
            bool found = false;
            for (std::size_t i = 1; i < operands.size(); ++i) {
                const auto candidate = ResolveTextOperand(query, match, operands[i], sourceText);
                if (candidate && *candidate == *text) {
                    found = true;
                    break;
                }
            }
            return negated ? !found : found;
        }

        if (baseName == "has-ancestor?" || baseName == "has-parent?") {
            if (operands.size() != 2 || operands[0].type != TSQueryPredicateStepTypeCapture ||
                operands[1].type != TSQueryPredicateStepTypeString) {
                return true;
            }
            const TSNode node = CapturedNode(match, operands[0].value_id);
            if (ts_node_is_null(node)) {
                return true;
            }
            uint32_t    length   = 0;
            const char* typeName = ts_query_string_value_for_id(query, operands[1].value_id, &length);
            const bool  has      = NodeHasAncestorOfType(node, std::string_view(typeName, length), baseName == "has-parent?");
            return negated ? !has : has;
        }

        return true; // unrecognized predicate name (e.g. "set!") -- inert
    }

    // Splits pattern's flat predicate-step array (Done steps are the
    // separators between individual "#name? operand..." calls) and invokes
    // fn(name, operands) for each -- shared by EvaluatePredicates below and
    // ExtractSetDirectives (Matches()'s own #set!-reading counterpart),
    // rather than duplicating this step-splitting walk twice.
    template <typename Fn>
    void ForEachPredicate(const TSQuery* query, const TSQueryMatch& match, Fn&& fn) {
        uint32_t                    stepCount = 0;
        const TSQueryPredicateStep* steps     = ts_query_predicates_for_pattern(query, match.pattern_index, &stepCount);

        std::size_t start = 0;
        for (uint32_t i = 0; i < stepCount; ++i) {
            if (steps[i].type != TSQueryPredicateStepTypeDone) {
                continue;
            }
            // steps[start] is always the predicate's own name (a String
            // step); steps[start+1 .. i) are its operands.
            if (i > start && steps[start].type == TSQueryPredicateStepTypeString) {
                uint32_t                                nameLength = 0;
                const char*                             name       = ts_query_string_value_for_id(query, steps[start].value_id, &nameLength);
                const std::vector<TSQueryPredicateStep> operands(steps + start + 1, steps + i);
                fn(std::string_view(name, nameLength), operands);
            }
            start = i + 1;
        }
    }

    // Evaluates every predicate against match, AND semantics (tree-sitter's
    // own documented predicate contract). Doesn't short-circuit the
    // step-array walk itself (ForEachPredicate has no early-exit), but does
    // skip calling the (comparatively expensive -- regex/ancestor-walk)
    // EvaluateOnePredicate once a prior predicate has already failed.
    bool EvaluatePredicates(const TSQuery* query, const TSQueryMatch& match, std::string_view sourceText,
                            std::unordered_map<std::string, std::regex>& regexCache) {
        bool passed = true;
        ForEachPredicate(query, match, [&](std::string_view name, const std::vector<TSQueryPredicateStep>& operands) {
            if (passed && !EvaluateOnePredicate(query, match, name, operands, sourceText, regexCache)) {
                passed = false;
            }
        });
        return passed;
    }

    // Resolves every #set! directive on match's pattern into a
    // {key -> value} map -- the one place #set!'s operands actually get
    // read; EvaluateOnePredicate above still treats #set! as inert for
    // filtering purposes (matches Query.h's documented "unrecognized
    // predicate never suppresses a match" contract -- #set! genuinely is
    // non-filtering, this is a separate, additive read of its data, not a
    // change to whether a match passes). A zero-operand #set! (e.g.
    // `(#set! injection.combined)`) stores an empty value so callers can
    // still test for the key's presence.
    std::unordered_map<std::string, std::string> ExtractSetDirectives(const TSQuery* query, const TSQueryMatch& match,
                                                                      std::string_view sourceText) {
        std::unordered_map<std::string, std::string> directives;
        ForEachPredicate(query, match, [&](std::string_view name, const std::vector<TSQueryPredicateStep>& operands) {
            if (!name.empty() && name.front() == '#') {
                name.remove_prefix(1);
            }
            if (name != "set!" || operands.empty()) {
                return;
            }
            const auto key = ResolveTextOperand(query, match, operands[0], sourceText);
            if (!key) {
                return;
            }
            std::string value;
            if (operands.size() > 1) {
                if (const auto resolved = ResolveTextOperand(query, match, operands[1], sourceText)) {
                    value = std::string(*resolved);
                }
            }
            directives[std::string(*key)] = std::move(value);
        });
        return directives;
    }

} // namespace

Query::Query(const Language& language, std::string_view source) {
    uint32_t     errorOffset = 0;
    TSQueryError errorType   = TSQueryErrorNone;

    query_ = ts_query_new(language.Raw(), source.data(), static_cast<uint32_t>(source.size()), &errorOffset,
                          &errorType);

    if (query_ == nullptr) {
        throw std::runtime_error("ned: tree-sitter query error (" + std::string(QueryErrorName(errorType)) +
                                 ") at byte offset " + std::to_string(errorOffset));
    }
}

Query::~Query() {
    ts_query_delete(query_); // ts_query_delete(nullptr) is a documented no-op
}

Query::Query(Query&& other) noexcept : query_(std::exchange(other.query_, nullptr)) {
}

Query& Query::operator=(Query&& other) noexcept {
    if (this != &other) {
        ts_query_delete(query_);
        query_ = std::exchange(other.query_, nullptr);
    }
    return *this;
}

std::vector<QueryCapture> Query::Captures(const Node& root, std::string_view sourceText) const {
    std::vector<QueryCapture> captures;

    TSQueryCursor* cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query_, root.Raw());

    // ts_query_cursor_next_capture yields one capture per call but can call
    // back with the *same* match (same match.id) repeatedly for a match
    // with several captures -- predicates are evaluated once per match, not
    // once per capture, both for correctness (a failing predicate must
    // suppress every capture of that match, not just whichever one happened
    // to be current) and to avoid redundant re-evaluation.
    TSQueryMatch match;
    uint32_t     captureIndex    = 0;
    uint32_t     lastMatchId     = 0;
    bool         havePassResult  = false;
    bool         lastMatchPassed = false;
    while (ts_query_cursor_next_capture(cursor, &match, &captureIndex)) {
        if (!havePassResult || match.id != lastMatchId) {
            lastMatchId     = match.id;
            lastMatchPassed = EvaluatePredicates(query_, match, sourceText, regexCache_);
            havePassResult  = true;
        }
        if (!lastMatchPassed) {
            continue;
        }

        const TSQueryCapture& capture = match.captures[captureIndex];

        uint32_t          nameLength = 0;
        const char* const name       = ts_query_capture_name_for_id(query_, capture.index, &nameLength);

        captures.push_back(QueryCapture{
            .name      = std::string(name, nameLength),
            .startByte = ts_node_start_byte(capture.node),
            .endByte   = ts_node_end_byte(capture.node),
        });
    }

    ts_query_cursor_delete(cursor);
    return captures;
}

std::vector<QueryMatch> Query::Matches(const Node& root, std::string_view sourceText) const {
    std::vector<QueryMatch> matches;

    TSQueryCursor* cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query_, root.Raw());

    // Match-at-a-time (unlike Captures()'s capture-at-a-time loop) -- this
    // is the whole point: match.captures[] is available in full for each
    // match, so captures from one pattern instance can be kept together
    // instead of flattened.
    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        if (!EvaluatePredicates(query_, match, sourceText, regexCache_)) {
            continue;
        }

        QueryMatch out;
        out.captures.reserve(match.capture_count);
        for (uint16_t i = 0; i < match.capture_count; ++i) {
            const TSQueryCapture& capture = match.captures[i];

            uint32_t          nameLength = 0;
            const char* const name       = ts_query_capture_name_for_id(query_, capture.index, &nameLength);

            out.captures.push_back(QueryMatchCapture{
                .name      = std::string(name, nameLength),
                .startByte = ts_node_start_byte(capture.node),
                .endByte   = ts_node_end_byte(capture.node),
            });
        }
        out.setDirectives = ExtractSetDirectives(query_, match, sourceText);
        matches.push_back(std::move(out));
    }

    ts_query_cursor_delete(cursor);
    return matches;
}

} // namespace ned::editor::treesitter
