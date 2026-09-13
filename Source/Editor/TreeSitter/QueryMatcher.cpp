#include "QueryMatcher.h"

#include "Editor/Parse/Cursor.h"
#include "Editor/Parse/LanguageTables.h"
#include "Editor/Parse/Node.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "QueryPredicates.h"

namespace ned::editor::treesitter {

namespace {

    using querydata::Form;

    // ---------------------------------------------------------------------
    // Enumeration policies tree-sitter does not document, centralized so the
    // M3 differential gate adjusts one constant instead of hunting logic.
    // ---------------------------------------------------------------------

    // A '*' item matches runs of consecutive named siblings. MaximalOnly
    // culls a start whose preceding named sibling also matches (a suffix of
    // a longer run never enumerates as its own assignment) and always
    // extends a run as far as it will go; the zero-length run is still
    // offered (a '*' is genuinely optional).
    constexpr bool kStarMaximalOnly = true;

    // Whether an alternation whose node matches several branches yields one
    // assignment per matching branch (tree-sitter's state machine forks per
    // branch) or just the first.
    constexpr bool kAlternationAllBranches = true;

    // ---------------------------------------------------------------------
    // Compiled pattern model.
    // ---------------------------------------------------------------------

    struct ChildItem;

    struct PatternNode {
        enum class Kind {
            Named,       // (type ...)
            Supertype,   // a grammar-declared supertype -- matches any subtype instance
            Anonymous,   // "literal"
            AnyNode,     // bare _
            AnyNamed,    // (_) -- possibly with children
            Error,       // (ERROR)
            Alternation, // [...] -- children are the branches
            Group,       // ((a) (b) ...) -- children are a sibling run; top-level pattern roots only
        };
        Kind                                   kind = Kind::Named;
        std::string                            type;           // Named/Anonymous/Supertype (name, for diagnostics)
        std::vector<parse::abi::Symbol>        symbols;        // Named/Anonymous: the symbols whose name is `type` (several
                                                               // when aliases share it) -- matched instead of the name string
        std::unordered_set<parse::abi::Symbol> subtypeSymbols; // Supertype: the transitive concrete subtype set
        std::vector<ChildItem>                 children;       // Named/AnyNamed children; Alternation branches; Group items
        std::vector<parse::abi::FieldId>       negatedFields;
        bool                                   trailingAnchor = false;
    };

    struct ChildItem {
        PatternNode           node;
        parse::abi::FieldId   field        = 0;
        char                  quantifier   = 0; // 0, '*', '?'
        bool                  anchorBefore = false;
        std::vector<uint32_t> captures; // capture ids bound to this item's matched node (one binding per rep for '*')
        int                   line = 0;
    };

    struct CompiledOperand {
        bool        isCapture = false;
        std::string text; // capture name (resolved to captureId post-compile) or literal text
        uint32_t    captureId = 0;
        int         line      = 0;
    };

    struct CompiledPredicate {
        std::string                  name;
        std::vector<CompiledOperand> operands;
        int                          line = 0;
    };

    struct Pattern {
        ChildItem                      root;
        std::vector<CompiledPredicate> predicates;
        // per-subtree-fact-memoization follow-up: true when any predicate on
        // this pattern is an actually-evaluated (not the arity-inert
        // variadic spelling -- see PredicateReadsOutsideSubtree)
        // (not-)has-ancestor?/(not-)has-parent? call, meaning a match of
        // this pattern can read structure OUTSIDE the node it's attached
        // to. Computed once at compile time (CompileTopLevel) and surfaced
        // per match as QueryMatch::ancestorCrossing -- a per-subtree fact
        // cache must always fully re-derive such a match rather than reuse
        // it across a reparse, even when the underlying subtree is
        // byte-for-byte unchanged, because its ancestry may not be.
        bool readsOutsideSubtree = false;
    };

    struct Binding {
        uint32_t       captureId;
        parse::RedNode node;
    };

    struct ChildInfo {
        parse::RedNode      node;
        parse::abi::FieldId field;
        bool                named;
    };

    void CollectChildren(parse::RedNode parent, std::vector<ChildInfo>& out) {
        out.clear();
        parse::TreeCursor cursor(parent);
        if (cursor.GotoFirstChild()) {
            do {
                const parse::RedNode node = cursor.CurrentNode();
                out.push_back(ChildInfo{node, cursor.CurrentFieldId(), parse::NodeIsNamed(node)});
            }
            while (cursor.GotoNextSibling());
        }
    }

    const std::vector<uint32_t> kNoCaptures;

} // namespace

QueryMatcherError::QueryMatcherError(int line, const std::string& message) : std::runtime_error("query line " + std::to_string(line) + ": " + message), line_(line) {
}

struct QueryMatcher::Impl {
    const parse::abi::LanguageData* language = nullptr;
    std::vector<Pattern>            patterns;
    std::vector<std::string>        captureNames;

    // Root dispatch, all pruning and no reordering: a node's trial sequence
    // is the indexed bucket then the unindexed patterns, ascending pattern
    // index within each -- exactly the old order minus trials whose root
    // provably cannot match this node's symbol (which contribute nothing,
    // so skipping them cannot change any output). Named/Anonymous roots
    // index by symbol; an alternation/supertype root whose branches all
    // resolve to concrete symbols gets a per-symbol candidate list; only a
    // genuinely wildcard-ish root (bare `_`, `(_)`, ERROR, group) is still
    // tried at every node.
    std::unordered_map<parse::abi::Symbol, std::vector<std::size_t>> rootIndex;
    std::unordered_map<parse::abi::Symbol, std::vector<std::size_t>> unindexedBySymbol;
    std::vector<std::size_t>                                         unfilteredUnindexed;

    mutable std::unordered_map<std::string, std::regex> regexCache;
    mutable std::string_view                            sourceText; // set per run
    mutable std::size_t                                 rangeStart = 0;
    mutable std::size_t                                 rangeEnd   = static_cast<std::size_t>(-1);

    // Every node the current in-progress assignment has matched, captured or
    // not -- same push/restore discipline as the bindings trail. The last of
    // them in pre-order is where tree-sitter's state machine COMPLETES the
    // match, which is the second key of capture emission order (see
    // CollectCaptures).
    mutable std::vector<parse::RedNode> matchedTrail;

    // ------------------------------------------------------------------
    // Compilation.
    // ------------------------------------------------------------------

    // Name -> every symbol carrying it: a name is not unique (an alias can
    // share a real rule's name), so matching compares the node's symbol
    // against the full set -- equivalent to the name comparison, minus the
    // per-node strcmp.
    std::unordered_map<std::string, std::vector<parse::abi::Symbol>> namedTypes;
    std::unordered_map<std::string, std::vector<parse::abi::Symbol>> anonymousTypes;
    std::unordered_map<std::string, parse::abi::Symbol>              supertypes;

    void BuildTypeTables() {
        const uint32_t count = parse::LanguageSymbolCount(language);
        for (uint32_t s = 0; s < count; ++s) {
            const char*             name = parse::LanguageSymbolName(language, static_cast<parse::abi::Symbol>(s));
            const parse::SymbolType type = parse::LanguageSymbolType(language, static_cast<parse::abi::Symbol>(s));
            if (type == parse::SymbolType::Regular) {
                namedTypes[name].push_back(static_cast<parse::abi::Symbol>(s));
            }
            else if (type == parse::SymbolType::Anonymous) {
                anonymousTypes[name].push_back(static_cast<parse::abi::Symbol>(s));
            }
        }
        uint32_t                  supertypeCount = 0;
        const parse::abi::Symbol* list           = parse::LanguageSupertypes(language, &supertypeCount);
        for (uint32_t i = 0; i < supertypeCount; ++i) {
            supertypes[parse::LanguageSymbolName(language, list[i])] = list[i];
        }
    }

    // The transitive concrete subtype set of a supertype -- a subtype may
    // itself be a supertype (measured possible, guarded regardless).
    std::unordered_set<parse::abi::Symbol> ExpandSupertype(parse::abi::Symbol supertype) const {
        std::unordered_set<parse::abi::Symbol> expanded;
        std::unordered_set<parse::abi::Symbol> visited;
        std::vector<parse::abi::Symbol>        pending{supertype};
        while (!pending.empty()) {
            const parse::abi::Symbol current = pending.back();
            pending.pop_back();
            if (!visited.insert(current).second) {
                continue;
            }
            uint32_t                  count = 0;
            const parse::abi::Symbol* subs  = parse::LanguageSubtypes(language, current, &count);
            for (uint32_t i = 0; i < count; ++i) {
                if (parse::LanguageSymbolType(language, subs[i]) == parse::SymbolType::Supertype) {
                    pending.push_back(subs[i]);
                }
                else {
                    expanded.insert(subs[i]);
                }
            }
        }
        return expanded;
    }

    uint32_t CaptureId(std::string_view name) {
        for (std::size_t i = 0; i < captureNames.size(); ++i) {
            if (captureNames[i] == name) {
                return static_cast<uint32_t>(i);
            }
        }
        captureNames.emplace_back(name);
        return static_cast<uint32_t>(captureNames.size() - 1);
    }

    parse::abi::FieldId FieldIdOrThrow(std::string_view name, int line) const {
        const parse::abi::FieldId id = parse::LanguageFieldIdForName(language, name.data(), static_cast<uint32_t>(name.size()));
        if (id == 0) {
            throw QueryMatcherError(line, "unknown field name '" + std::string(name) + "'");
        }
        return id;
    }

    // Compiles one pattern form (List/Alternation/String/bare-wildcard
    // Symbol) into a PatternNode. Predicates found anywhere inside attach
    // to `pattern`. A single-pattern paren group -- the ((x) (#pred))
    // idiom -- collapses to its one item; the collapsed item's own trailing
    // captures come back through `hoistedCaptures`.
    PatternNode CompileNode(const Form& form, Pattern& pattern, std::vector<uint32_t>& hoistedCaptures,
                            bool allowGroup) {
        switch (form.kind) {
            case Form::Kind::String: {
                const auto symbols = anonymousTypes.find(form.text);
                if (symbols == anonymousTypes.end()) {
                    throw QueryMatcherError(form.line, "unknown anonymous node '" + form.text + "'");
                }
                PatternNode node;
                node.kind    = PatternNode::Kind::Anonymous;
                node.type    = form.text;
                node.symbols = symbols->second;
                return node;
            }
            case Form::Kind::Symbol: {
                if (form.text == "_") {
                    PatternNode node;
                    node.kind = PatternNode::Kind::AnyNode;
                    return node;
                }
                throw QueryMatcherError(form.line, "bare symbol '" + form.text + "' is not a supported pattern");
            }
            case Form::Kind::Alternation: {
                PatternNode node;
                node.kind = PatternNode::Kind::Alternation;
                CompileSequence(form.items, node, pattern, SequenceContext::Alternation);
                if (node.children.empty()) {
                    throw QueryMatcherError(form.line, "empty alternation");
                }
                return node;
            }
            case Form::Kind::List:
                return CompileList(form, pattern, hoistedCaptures, allowGroup);
            case Form::Kind::Predicate:
            case Form::Kind::Comment:
                break;
        }
        throw QueryMatcherError(form.line, "unsupported form in pattern position");
    }

    PatternNode CompileList(const Form& form, Pattern& pattern, std::vector<uint32_t>& hoistedCaptures,
                            bool allowGroup) {
        const Form* head = nullptr;
        for (const Form& item : form.items) {
            if (item.kind != Form::Kind::Comment) {
                head = &item;
                break;
            }
        }
        if (head == nullptr) {
            throw QueryMatcherError(form.line, "empty pattern");
        }

        const bool headIsTypeSymbol =
            head->kind == Form::Kind::Symbol && head->text != "." && !head->text.empty() &&
            head->text.front() != '@' && head->text.front() != '!' && head->text.back() != ':';
        if (headIsTypeSymbol) {
            PatternNode node;
            if (head->text == "_") {
                node.kind = PatternNode::Kind::AnyNamed;
            }
            else if (head->text == "ERROR") {
                node.kind = PatternNode::Kind::Error;
            }
            else if (const auto symbols = namedTypes.find(head->text); symbols != namedTypes.end()) {
                node.kind    = PatternNode::Kind::Named;
                node.type    = head->text;
                node.symbols = symbols->second;
            }
            else if (const auto supertype = supertypes.find(head->text); supertype != supertypes.end()) {
                node.kind           = PatternNode::Kind::Supertype;
                node.type           = head->text;
                node.subtypeSymbols = ExpandSupertype(supertype->second);
            }
            else {
                throw QueryMatcherError(head->line, "unknown node type '" + head->text + "'");
            }
            const std::size_t       headIndex = static_cast<std::size_t>(head - form.items.data());
            const std::vector<Form> children(form.items.begin() + static_cast<std::ptrdiff_t>(headIndex) + 1,
                                             form.items.end());
            CompileSequence(children, node, pattern, SequenceContext::NodeChildren);
            return node;
        }

        // A paren group: a sibling run.
        PatternNode group;
        group.kind = PatternNode::Kind::Group;
        CompileSequence(form.items, group, pattern, SequenceContext::Group);
        if (group.children.empty()) {
            throw QueryMatcherError(form.line, "empty group");
        }
        if (group.children.size() == 1 && !group.trailingAnchor) {
            ChildItem& only = group.children.front();
            if (only.field == 0 && !only.anchorBefore && only.quantifier == 0 && group.negatedFields.empty()) {
                hoistedCaptures = std::move(only.captures);
                return std::move(only.node);
            }
        }
        if (!allowGroup) {
            throw QueryMatcherError(form.line, "a multi-pattern group is only supported at the top level");
        }
        return group;
    }

    enum class SequenceContext { TopLevel,
                                 NodeChildren,
                                 Group,
                                 Alternation };

    // Compiles a form sequence into parent.children: anchors, fields,
    // negated fields, captures, predicates, pattern items. For an
    // alternation, items are branches -- captures still attach to the
    // preceding branch, everything else positional is outside the census
    // and rejected.
    void CompileSequence(const std::vector<Form>& forms, PatternNode& parent, Pattern& pattern,
                         SequenceContext context) {
        const bool          isAlternation    = context == SequenceContext::Alternation;
        bool                pendingAnchor    = false;
        parse::abi::FieldId pendingField     = 0;
        int                 pendingFieldLine = 0;

        for (const Form& form : forms) {
            if (form.kind == Form::Kind::Comment) {
                continue;
            }
            if (form.kind == Form::Kind::Predicate) {
                if (isAlternation) {
                    throw QueryMatcherError(form.line, "predicate inside an alternation is not supported");
                }
                CompilePredicate(form, pattern);
                continue;
            }
            if (form.kind == Form::Kind::Symbol && form.text != "_") {
                if (form.text == ".") {
                    if (isAlternation) {
                        throw QueryMatcherError(form.line, "anchor inside an alternation is not supported");
                    }
                    pendingAnchor = true;
                    continue;
                }
                if (form.text.empty()) {
                    throw QueryMatcherError(form.line, "empty symbol");
                }
                if (form.text.front() == '@') {
                    if (parent.children.empty()) {
                        throw QueryMatcherError(form.line, "capture '" + form.text + "' has nothing to attach to");
                    }
                    parent.children.back().captures.push_back(CaptureId(std::string_view(form.text).substr(1)));
                    continue;
                }
                if (form.text.front() == '!') {
                    if (isAlternation) {
                        throw QueryMatcherError(form.line, "negated field inside an alternation is not supported");
                    }
                    parent.negatedFields.push_back(FieldIdOrThrow(std::string_view(form.text).substr(1), form.line));
                    continue;
                }
                if (form.text.back() == ':') {
                    // Inside an alternation a field constrains that branch
                    // alone (php: `[... alias: (name) @type]`).
                    pendingField     = FieldIdOrThrow(std::string_view(form.text).substr(0, form.text.size() - 1),
                                                      form.line);
                    pendingFieldLine = form.line;
                    continue;
                }
                throw QueryMatcherError(form.line, "unsupported bare symbol '" + form.text + "'");
            }

            // A pattern item.
            ChildItem item;
            item.line         = form.line;
            item.anchorBefore = pendingAnchor;
            item.field        = pendingField;
            item.quantifier   = form.quantifier;
            pendingAnchor     = false;
            pendingField      = 0;

            std::vector<uint32_t> hoisted;
            item.node     = CompileNode(form, pattern, hoisted, /*allowGroup=*/context == SequenceContext::TopLevel);
            item.captures = std::move(hoisted);

            // Quantifiers are accepted on any pattern kind and '+' alongside
            // the census's '*'/'?' -- the bundled files never use these (the
            // census pins that), but a foreign :queries-dir file (a system
            // tree-sitter install's own .scm) legally can, and rejecting it
            // would take the whole runtime language down.
            if (item.node.kind == PatternNode::Kind::Group && item.quantifier != 0) {
                throw QueryMatcherError(form.line, "quantifier on a multi-pattern group is not supported");
            }
            parent.children.push_back(std::move(item));
        }

        if (pendingField != 0) {
            throw QueryMatcherError(pendingFieldLine, "field prefix with no pattern after it");
        }
        if (pendingAnchor) {
            parent.trailingAnchor = true;
        }
    }

    void CompilePredicate(const Form& form, Pattern& pattern) {
        CompiledPredicate predicate;
        predicate.name = form.text;
        predicate.line = form.line;
        for (const Form& operand : form.items) {
            CompiledOperand compiled;
            compiled.line = operand.line;
            if (operand.kind == Form::Kind::String) {
                compiled.text = operand.text;
            }
            else if (operand.kind == Form::Kind::Symbol) {
                if (!operand.text.empty() && operand.text.front() == '@') {
                    compiled.isCapture = true;
                    compiled.text      = operand.text.substr(1);
                }
                else {
                    // A bare token operand -- through tree-sitter's own
                    // compiler this is the same String step a quoted one is.
                    compiled.text = operand.text;
                }
            }
            else {
                throw QueryMatcherError(operand.line, "unsupported predicate operand");
            }
            predicate.operands.push_back(std::move(compiled));
        }
        pattern.predicates.push_back(std::move(predicate));
    }

    void CompileTopLevel(std::span<const Form> forms) {
        for (std::size_t i = 0; i < forms.size();) {
            const Form& form = forms[i];
            if (form.kind == Form::Kind::Comment) {
                ++i;
                continue;
            }
            if (form.kind == Form::Kind::Predicate) {
                throw QueryMatcherError(form.line, "top-level predicate is not supported");
            }

            // This pattern's own form plus its trailing captures.
            std::vector<Form> slice;
            slice.push_back(form);
            ++i;
            while (i < forms.size()) {
                const Form& next = forms[i];
                if (next.kind == Form::Kind::Comment) {
                    ++i;
                    continue;
                }
                if (next.kind == Form::Kind::Symbol && !next.text.empty() && next.text.front() == '@') {
                    slice.push_back(next);
                    ++i;
                    continue;
                }
                break;
            }

            Pattern     pattern;
            PatternNode holder;
            holder.kind = PatternNode::Kind::Group;
            CompileSequence(slice, holder, pattern, SequenceContext::TopLevel);
            if (holder.children.size() != 1 || holder.trailingAnchor || holder.children.front().anchorBefore) {
                throw QueryMatcherError(form.line, "malformed top-level pattern");
            }
            if (holder.children.front().quantifier != 0) {
                throw QueryMatcherError(form.line, "quantifier on a top-level pattern is not supported");
            }
            pattern.root = std::move(holder.children.front());
            patterns.push_back(std::move(pattern));
        }

        // Resolve predicate capture references now that every capture name
        // is known -- a reference no pattern declares is an error,
        // tree-sitter's own TSQueryErrorCapture.
        for (Pattern& pattern : patterns) {
            for (CompiledPredicate& predicate : pattern.predicates) {
                for (CompiledOperand& operand : predicate.operands) {
                    if (!operand.isCapture) {
                        continue;
                    }
                    bool found = false;
                    for (std::size_t id = 0; id < captureNames.size(); ++id) {
                        if (captureNames[id] == operand.text) {
                            operand.captureId = static_cast<uint32_t>(id);
                            found             = true;
                            break;
                        }
                    }
                    if (!found) {
                        throw QueryMatcherError(operand.line,
                                                "predicate references unknown capture '@" + operand.text + "'");
                    }
                }
            }
        }

        for (Pattern& pattern : patterns) {
            for (const CompiledPredicate& predicate : pattern.predicates) {
                if (PredicateReadsOutsideSubtree(predicate.name, predicate.operands.size())) {
                    pattern.readsOutsideSubtree = true;
                    break;
                }
            }
        }

        for (std::size_t p = 0; p < patterns.size(); ++p) {
            const PatternNode& root = patterns[p].root.node;
            if (root.kind == PatternNode::Kind::Named || root.kind == PatternNode::Kind::Anonymous) {
                for (const parse::abi::Symbol symbol : root.symbols) {
                    rootIndex[symbol].push_back(p);
                }
                continue;
            }
            std::unordered_set<parse::abi::Symbol> filter;
            if (RootSymbolFilter(root, filter)) {
                for (const parse::abi::Symbol symbol : filter) {
                    unindexedBySymbol[symbol].push_back(p);
                }
            }
            else {
                unfilteredUnindexed.push_back(p);
            }
        }
        // Every per-symbol list is ascending by construction (the loop
        // above appends in ascending p), which RunAtNode's order-preserving
        // merge depends on.
    }

    // The set of symbols this root could possibly match, or false when it
    // can match anything concrete symbols can't enumerate (wildcards,
    // ERROR, group roots). Over-approximation would be fine; under-
    // approximation would silently drop matches, so anything uncertain
    // returns false.
    static bool RootSymbolFilter(const PatternNode& root, std::unordered_set<parse::abi::Symbol>& filter) {
        switch (root.kind) {
            case PatternNode::Kind::Named:
            case PatternNode::Kind::Anonymous:
                filter.insert(root.symbols.begin(), root.symbols.end());
                return true;
            case PatternNode::Kind::Supertype:
                filter.insert(root.subtypeSymbols.begin(), root.subtypeSymbols.end());
                return true;
            case PatternNode::Kind::Alternation:
                for (const ChildItem& branch : root.children) {
                    if (!RootSymbolFilter(branch.node, filter)) {
                        return false;
                    }
                }
                return true;
            case PatternNode::Kind::AnyNode:
            case PatternNode::Kind::AnyNamed:
            case PatternNode::Kind::Error:
            case PatternNode::Kind::Group:
                break;
        }
        return false;
    }

    // ------------------------------------------------------------------
    // Matching. Continuation style throughout: bindings are extended only
    // for the duration of the continuation, and every function restores
    // the vector to its entry size before returning -- which is what makes
    // full backtracking enumeration correct with one shared vector.
    // ------------------------------------------------------------------

    using MatchFn = std::function<void()>;

    // Matches `pattern` against exactly `node` (quantifiers/anchors are the
    // sequence layer's business). `nodeField` is the field the node holds in
    // its own parent (0 = none/unknown) -- an alternation's field-prefixed
    // branch constrains against it. Binds `captures` plus everything inside,
    // invoking `next` once per complete assignment.
    // The symbol comparison that replaces `pattern.type != ts_node_type(node)`
    // plus the namedness check: ts_node_symbol resolves aliases exactly the
    // way ts_node_type does, and named/anonymous symbol sets are disjoint.
    // The list is almost always one entry, so a linear scan beats any set.
    static bool SymbolMatches(const std::vector<parse::abi::Symbol>& symbols, parse::RedNode node) {
        const parse::abi::Symbol symbol = parse::NodeSymbol(node);
        for (const parse::abi::Symbol candidate : symbols) {
            if (candidate == symbol) {
                return true;
            }
        }
        return false;
    }

    void MatchNode(const PatternNode& pattern, const std::vector<uint32_t>& captures, parse::RedNode node,
                   parse::abi::FieldId nodeField, std::vector<Binding>& bindings, const MatchFn& next) const {
        switch (pattern.kind) {
            case PatternNode::Kind::AnyNode:
                break;
            case PatternNode::Kind::AnyNamed:
                if (!parse::NodeIsNamed(node)) {
                    return;
                }
                break;
            case PatternNode::Kind::Error:
                if (!parse::NodeIsError(node)) {
                    return;
                }
                break;
            case PatternNode::Kind::Named:
                // Symbol membership implies namedness: `symbols` only ever
                // holds parse::SymbolType::Regular entries (BuildTypeTables).
                if (!SymbolMatches(pattern.symbols, node)) {
                    return;
                }
                break;
            case PatternNode::Kind::Supertype:
                if (!parse::NodeIsNamed(node) || !pattern.subtypeSymbols.contains(parse::NodeSymbol(node))) {
                    return;
                }
                break;
            case PatternNode::Kind::Anonymous:
                if (!SymbolMatches(pattern.symbols, node)) {
                    return;
                }
                break;
            case PatternNode::Kind::Alternation: {
                const parse::abi::Symbol nodeSymbol = parse::NodeSymbol(node);
                for (const ChildItem& branch : pattern.children) {
                    if (branch.field != 0 && branch.field != nodeField) {
                        continue; // a field-prefixed branch constrains this branch alone
                    }
                    // Cheap pre-gate: a concrete branch whose symbol set
                    // excludes this node would be refused by the recursive
                    // MatchNode anyway -- skip the binding push/pop.
                    if ((branch.node.kind == PatternNode::Kind::Named ||
                         branch.node.kind == PatternNode::Kind::Anonymous) &&
                        std::find(branch.node.symbols.begin(), branch.node.symbols.end(), nodeSymbol) ==
                            branch.node.symbols.end()) {
                        continue;
                    }
                    const std::size_t mark    = bindings.size();
                    bool              matched = false;
                    // Outer captures first (they were the caller's), then
                    // the branch's own -- tree-sitter's step order puts a
                    // node's captures at the step itself, and the branch
                    // node IS the step here.
                    for (const uint32_t id : captures) {
                        bindings.push_back(Binding{id, node});
                    }
                    MatchNode(branch.node, branch.captures, node, nodeField, bindings, [&] {
                        matched = true;
                        next();
                    });
                    bindings.resize(mark);
                    if (matched && !kAlternationAllBranches) {
                        return;
                    }
                }
                return;
            }
            case PatternNode::Kind::Group:
                return; // only ever a pattern root; handled in TryPattern
        }

        const std::size_t mark      = bindings.size();
        const std::size_t trailMark = matchedTrail.size();
        matchedTrail.push_back(node);
        for (const uint32_t id : captures) {
            bindings.push_back(Binding{id, node});
        }
        bool fieldsOk = true;
        for (const parse::abi::FieldId field : pattern.negatedFields) {
            if (!parse::NodeIsNull(parse::NodeChildByFieldId(node, field))) {
                fieldsOk = false;
                break;
            }
        }
        if (fieldsOk) {
            if (pattern.children.empty() && !pattern.trailingAnchor) {
                next();
            }
            else {
                std::vector<ChildInfo> children;
                CollectChildren(node, children);
                EnumSequence(pattern, 0, children, 0, -1, false, /*bounded=*/true, bindings, next);
            }
        }
        bindings.resize(mark);
        matchedTrail.resize(trailMark);
    }

    static bool NamedBetween(const std::vector<ChildInfo>& children, std::ptrdiff_t a, std::ptrdiff_t b) {
        for (std::ptrdiff_t i = a + 1; i < b; ++i) {
            if (children[static_cast<std::size_t>(i)].named) {
                return true;
            }
        }
        return false;
    }

    // Enumerates assignments of parent.children[itemIdx..] against
    // children[childIdx..]. `lastMatched` is the child index the previous
    // item bound (-1: none yet); `carriedAnchor` is an anchor owed since
    // then (an anchor before an item that matched zero nodes carries to
    // the next). `bounded` distinguishes a node pattern's children
    // (leading/trailing anchors bind to the child list's real edges) from
    // a top-level group's float (an edge anchor with no matched neighbor
    // is inert).
    void EnumSequence(const PatternNode& parent, std::size_t itemIdx, const std::vector<ChildInfo>& children,
                      std::size_t childIdx, std::ptrdiff_t lastMatched, bool carriedAnchor, bool bounded,
                      std::vector<Binding>& bindings, const MatchFn& next) const {
        if (itemIdx == parent.children.size()) {
            // Only an explicitly written trailing '.' constrains the end; an
            // anchor carried here through a zero-matched optional does not
            // (measured: clojure's `. (sym_lit)? @function . (str_lit)?`
            // still matches when both optionals are absent and more
            // children follow).
            if (parent.trailingAnchor && bounded) {
                for (std::size_t i = lastMatched < 0 ? 0 : static_cast<std::size_t>(lastMatched) + 1;
                     i < children.size(); ++i) {
                    if (children[i].named) {
                        return;
                    }
                }
            }
            next();
            return;
        }

        const ChildItem& item     = parent.children[itemIdx];
        const bool       anchored = item.anchorBefore || carriedAnchor;

        // An optional that CAN match, must -- tree-sitter's automaton
        // consumes greedily rather than forking a skip state (measured:
        // clojure's `(sym_lit)? @function` yields one match with the name
        // bound, never a second with the optional empty). The zero branch
        // fires only when no candidate matched; for '*' the zero-length run
        // is offered unconditionally (measured the other way: go's
        // `((comment)* . (func))` tags a comment-less function). '+' is a
        // run with no zero branch.
        if (item.quantifier == '*') {
            EnumSequence(parent, itemIdx + 1, children, childIdx, lastMatched, anchored, bounded, bindings, next);
        }
        bool anyOccurrence = false;

        for (std::size_t c = childIdx; c < children.size(); ++c) {
            const ChildInfo& child = children[c];
            if (item.field != 0 && child.field != item.field) {
                continue;
            }
            if (anchored) {
                if (lastMatched < 0) {
                    if (bounded && NamedBetween(children, -1, static_cast<std::ptrdiff_t>(c))) {
                        continue; // leading anchor: no named child before c
                    }
                }
                else if (NamedBetween(children, lastMatched, static_cast<std::ptrdiff_t>(c))) {
                    continue;
                }
            }

            if (item.quantifier == '*' || item.quantifier == '+') {
                if (kStarMaximalOnly && StartHasMatchingPredecessor(item, children, c, lastMatched)) {
                    continue; // a suffix of a longer run; that run enumerates separately
                }
                ExtendStarRun(parent, itemIdx, children, c, bounded, bindings, next);
                continue;
            }

            MatchNode(item.node, item.captures, child.node, child.field, bindings, [&] {
                anyOccurrence = true;
                EnumSequence(parent, itemIdx + 1, children, c + 1, static_cast<std::ptrdiff_t>(c), false, bounded,
                             bindings, next);
            });
        }

        if (item.quantifier == '?' && !anyOccurrence) {
            EnumSequence(parent, itemIdx + 1, children, childIdx, lastMatched, anchored, bounded, bindings, next);
        }
    }

    // The named sibling immediately before `start` (if any, and if after
    // lastMatched) also matches the '*' item's pattern -- meaning a run
    // starting at `start` is a suffix of a longer one.
    bool StartHasMatchingPredecessor(const ChildItem& item, const std::vector<ChildInfo>& children, std::size_t start,
                                     std::ptrdiff_t lastMatched) const {
        std::ptrdiff_t prev = static_cast<std::ptrdiff_t>(start) - 1;
        while (prev >= 0 && !children[static_cast<std::size_t>(prev)].named) {
            --prev;
        }
        if (prev < 0 || prev <= lastMatched) {
            return false;
        }
        const ChildInfo& before = children[static_cast<std::size_t>(prev)];
        return ProbeMatches(item.node, before.node, before.field);
    }

    bool ProbeMatches(const PatternNode& pattern, parse::RedNode node, parse::abi::FieldId nodeField) const {
        bool                 matched = false;
        std::vector<Binding> probe;
        MatchNode(pattern, kNoCaptures, node, nodeField, probe, [&] { matched = true; });
        return matched;
    }

    // One-or-more reps of a '*' item: rep at child index `c`, then extend
    // with the next named sibling while it matches, continuing the
    // sequence only past the maximal run (kStarMaximalOnly).
    void ExtendStarRun(const PatternNode& parent, std::size_t itemIdx, const std::vector<ChildInfo>& children,
                       std::size_t c, bool bounded, std::vector<Binding>& bindings, const MatchFn& next) const {
        const ChildItem& item = parent.children[itemIdx];
        MatchNode(item.node, item.captures, children[c].node, children[c].field, bindings, [&] {
            // The next rep candidate: the next named sibling, except for an
            // anonymous/any-node item pattern, whose reps can be anonymous.
            const bool  scanAnonymous = item.node.kind == PatternNode::Kind::Anonymous ||
                                        item.node.kind == PatternNode::Kind::AnyNode;
            std::size_t n             = c + 1;
            if (!scanAnonymous) {
                while (n < children.size() && !children[n].named) {
                    ++n;
                }
            }
            if (n < children.size() && ProbeMatches(item.node, children[n].node, children[n].field)) {
                ExtendStarRun(parent, itemIdx, children, n, bounded, bindings, next);
            }
            else {
                EnumSequence(parent, itemIdx + 1, children, c + 1, static_cast<std::ptrdiff_t>(c), false, bounded,
                             bindings, next);
            }
        });
    }

    // ------------------------------------------------------------------
    // Per-node pattern trial and the tree walk.
    // ------------------------------------------------------------------

    // `nodeField` is the field `node` holds in its own parent (0 =
    // none/unknown) -- it only matters for the rare
    // alternation-with-field-branches root, and the walk reads it off its
    // cursor for free.
    void TryPattern(std::size_t patternIndex, parse::RedNode node, parse::abi::FieldId nodeField, std::vector<Binding>& bindings,
                    const MatchFn& next) const {
        const ChildItem& root = patterns[patternIndex].root;
        if (root.node.kind == PatternNode::Kind::Group) {
            std::vector<ChildInfo> children;
            CollectChildren(node, children);
            if (children.empty()) {
                return;
            }
            EnumSequence(root.node, 0, children, 0, -1, false, /*bounded=*/false, bindings, next);
            return;
        }
        MatchNode(root.node, root.captures, node, nodeField, bindings, next);
    }

    static parse::abi::FieldId FieldOfNode(parse::RedNode node) {
        const parse::RedNode parent = parse::NodeParent(node);
        if (parse::NodeIsNull(parent)) {
            return 0;
        }
        parse::abi::FieldId field = 0;
        parse::TreeCursor   cursor(parent);
        if (cursor.GotoFirstChild()) {
            do {
                if (parse::NodeEq(cursor.CurrentNode(), node)) {
                    field = cursor.CurrentFieldId();
                    break;
                }
            }
            while (cursor.GotoNextSibling());
        }
        return field;
    }

    template <typename Sink>
    void RunAtNode(parse::RedNode node, parse::abi::FieldId nodeField, Sink& sink) const {
        std::vector<Binding> bindings;
        const auto           tryOne = [&](std::size_t patternIndex) {
            bindings.clear();
            TryPattern(patternIndex, node, nodeField, bindings, [&] {
                if (EvaluatePatternPredicates(patternIndex, bindings)) {
                    sink(patternIndex, bindings);
                }
            });
        };

        const parse::abi::Symbol symbol = parse::NodeSymbol(node);
        if (const auto it = rootIndex.find(symbol); it != rootIndex.end()) {
            for (const std::size_t p : it->second) {
                tryOne(p);
            }
        }
        // The unindexed phase in ascending pattern order: the symbol's own
        // candidates merged with the try-everywhere set. Patterns pruned
        // here could not have matched, so the productive trial sequence is
        // byte-for-byte the old "every unindexed pattern" one.
        static const std::vector<std::size_t> kNone;
        const auto                            filteredIt = unindexedBySymbol.find(symbol);
        const std::vector<std::size_t>&       filtered   = filteredIt != unindexedBySymbol.end() ? filteredIt->second : kNone;
        std::size_t                           a          = 0;
        std::size_t                           b          = 0;
        while (a < filtered.size() || b < unfilteredUnindexed.size()) {
            if (b == unfilteredUnindexed.size() || (a < filtered.size() && filtered[a] < unfilteredUnindexed[b])) {
                tryOne(filtered[a++]);
            }
            else {
                tryOne(unfilteredUnindexed[b++]);
            }
        }
    }

    // A zero-width node (a MISSING token) intersects when it sits inside
    // the range; a real node when the spans overlap.
    static bool InRange(parse::RedNode node, std::size_t startByte, std::size_t endByte) {
        const std::size_t start = parse::NodeStartByte(node);
        const std::size_t end   = parse::NodeEndByte(node);
        if (start == end) {
            return start >= startByte && start < endByte;
        }
        return start < endByte && end > startByte;
    }

    // Visits the cursor's current node if it intersects the range,
    // returning whether it did -- an out-of-range node's subtree is pruned
    // (the walk never descends into an unvisited node), matching the old
    // recursive walk's entry check.
    template <typename Sink>
    bool VisitCurrent(parse::TreeCursor& cursor, std::size_t startByte, std::size_t endByte, Sink& sink) const {
        const parse::RedNode node = cursor.CurrentNode();
        if (!InRange(node, startByte, endByte)) {
            return false;
        }
        RunAtNode(node, cursor.CurrentFieldId(), sink);
        return true;
    }

    // Pre-order over the subtree via one TSTreeCursor. Deliberately not the
    // obvious ts_node_child(node, i) recursion: ts_node_child restarts its
    // sibling iteration from the first child on every call, making that
    // walk quadratic in child count (measured: the dominant cost of a full
    // highlights run over a large C++ file, alongside re-deriving each
    // node's field by scanning its parent's children -- the cursor hands
    // both out in O(1) as it goes).
    template <typename Sink>
    void Walk(parse::RedNode node, std::size_t startByte, std::size_t endByte, Sink& sink) const {
        if (!InRange(node, startByte, endByte)) {
            return;
        }
        // The entry node's own field comes from a one-time parent scan --
        // the cursor only knows fields below its construction point.
        RunAtNode(node, FieldOfNode(node), sink);
        parse::TreeCursor cursor(node);
        bool              mayDescend = true;
        for (;;) {
            if (mayDescend && cursor.GotoFirstChild()) {
                mayDescend = VisitCurrent(cursor, startByte, endByte, sink);
                continue;
            }
            if (cursor.GotoNextSibling()) {
                mayDescend = VisitCurrent(cursor, startByte, endByte, sink);
                continue;
            }
            if (!cursor.GotoParent()) {
                break; // back at the entry node: done
            }
            mayDescend = false; // the parent's subtree below is exhausted; advance
        }
    }

    // ------------------------------------------------------------------
    // Predicates and #set! -- Query.cpp parity via QueryPredicates.h.
    // ------------------------------------------------------------------

    static parse::RedNode FirstBound(const std::vector<Binding>& bindings, uint32_t captureId) {
        for (const Binding& binding : bindings) {
            if (binding.captureId == captureId) {
                return binding.node;
            }
        }
        return parse::NodeNull();
    }

    std::optional<std::string_view> NodeText(parse::RedNode node) const {
        if (parse::NodeIsNull(node)) {
            return std::nullopt;
        }
        const uint32_t start = parse::NodeStartByte(node);
        const uint32_t end   = parse::NodeEndByte(node);
        if (start > end || end > sourceText.size()) {
            return std::nullopt; // defensive -- ResolveTextOperand's own rule
        }
        return sourceText.substr(start, end - start);
    }

    bool EvaluatePatternPredicates(std::size_t patternIndex, const std::vector<Binding>& bindings) const {
        for (const CompiledPredicate& predicate : patterns[patternIndex].predicates) {
            std::vector<PredicateOperand> operands;
            operands.reserve(predicate.operands.size());
            for (const CompiledOperand& compiled : predicate.operands) {
                PredicateOperand operand;
                operand.isCapture = compiled.isCapture;
                if (compiled.isCapture) {
                    operand.node = FirstBound(bindings, compiled.captureId);
                    operand.text = NodeText(operand.node);
                }
                else {
                    operand.text = compiled.text;
                }
                operands.push_back(operand);
            }
            if (!EvaluatePredicateCall(predicate.name, operands, regexCache)) {
                return false;
            }
        }
        return true;
    }

    std::unordered_map<std::string, std::string> SetDirectives(std::size_t                 patternIndex,
                                                               const std::vector<Binding>& bindings) const {
        std::unordered_map<std::string, std::string> directives;
        for (const CompiledPredicate& predicate : patterns[patternIndex].predicates) {
            if (predicate.name != "set!" || predicate.operands.empty()) {
                continue;
            }
            const auto resolve = [&](const CompiledOperand& operand) -> std::optional<std::string_view> {
                if (!operand.isCapture) {
                    return std::string_view(operand.text);
                }
                return NodeText(FirstBound(bindings, operand.captureId));
            };
            const auto key = resolve(predicate.operands[0]);
            if (!key) {
                continue;
            }
            std::string value;
            if (predicate.operands.size() > 1) {
                if (const auto resolved = resolve(predicate.operands[1])) {
                    value = std::string(*resolved);
                }
            }
            directives[std::string(*key)] = std::move(value);
        }
        return directives;
    }

    // ------------------------------------------------------------------
    // Public-shape runs.
    // ------------------------------------------------------------------

    std::vector<QueryCapture> CollectCaptures(parse::RedNode root, std::string_view text, std::size_t startByte,
                                              std::size_t endByte) const {
        sourceText = text;
        rangeStart = startByte;
        rangeEnd   = endByte;
        // A (start, end) pair ordered as pre-order tree traversal visits
        // nodes: earlier start first; at the same start, the ancestor
        // (larger end) first.
        struct PreKey {
            std::size_t start = 0;
            std::size_t end   = 0;
            bool        operator<(const PreKey& other) const {
                if (start != other.start) {
                    return start < other.start;
                }
                return end > other.end;
            }
        };
        struct SimMatch {
            std::vector<QueryCapture> caps; // bind order
            std::size_t               pattern  = 0;
            std::size_t               consumed = 0;
            PreKey                    activate; // first capture's bind position
            PreKey                    finish;   // completion node's position
        };
        std::vector<SimMatch> sims;
        auto                  sink = [&](std::size_t patternIndex, const std::vector<Binding>& bindings) {
            if (bindings.empty()) {
                return;
            }
            SimMatch sim;
            sim.pattern = patternIndex;
            sim.caps.reserve(bindings.size());
            for (const Binding& binding : bindings) {
                sim.caps.push_back(QueryCapture{
                    .name      = captureNames[binding.captureId],
                    .startByte = parse::NodeStartByte(binding.node),
                    .endByte   = parse::NodeEndByte(binding.node),
                    .nodeId    = binding.node.id,
                });
            }
            sim.activate = PreKey{sim.caps.front().startByte, sim.caps.front().endByte};
            // The completion node: the last-visited node the assignment
            // matched, captured or not.
            for (const parse::RedNode& node : matchedTrail) {
                const PreKey key{parse::NodeStartByte(node), parse::NodeEndByte(node)};
                if (sim.finish < key) {
                    sim.finish = key;
                }
            }
            sims.push_back(std::move(sim));
        };
        Walk(root, startByte, endByte, sink);

        // Emission order is tree-sitter's incremental cursor merge, verified
        // against a raw-TSQueryCursor probe rather than guessed -- it is NOT
        // a sort: a finished match emits its captures in bind order by
        // (capture byte, pattern index) priority, but a capture is HELD
        // while any still-in-progress state's first pending capture sits at
        // or before it. That hold is what lets c's pattern-62 parameter
        // capture emit before pattern-15's operator at the same byte (the
        // operator's state doesn't exist yet when the parameter match
        // finishes) while clojure's pattern-1 @aligned waits for pattern-0's
        // same-byte @indent.body (pattern 0's capture is already pending).
        // A match is "in progress" from its first capture's bind position
        // (the captured node's pre-order visit) until its completion node's
        // visit -- the last node its pattern needed, so cpp's
        // `(function_definition type: (_) @type.return)` finishes at the
        // qualified_identifier while `(namespace_identifier) @module`
        // finishes at that node's own child, one visit later.
        struct Event {
            PreKey      at;
            bool        isFinish; // false: activate
            std::size_t sim;
        };
        std::vector<Event> events;
        events.reserve(sims.size() * 2);
        for (std::size_t i = 0; i < sims.size(); ++i) {
            events.push_back(Event{sims[i].activate, false, i});
            events.push_back(Event{sims[i].finish, true, i});
        }
        std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
            if (a.at < b.at) {
                return true;
            }
            if (b.at < a.at) {
                return false;
            }
            return !a.isFinish && b.isFinish; // activations before finishes at one visit
        });

        std::vector<std::size_t>  active;   // activated, not yet finished
        std::vector<std::size_t>  finished; // finished, captures not fully consumed
        std::vector<QueryCapture> out;
        const auto                priority = [&](std::size_t sim) {
            return std::pair<std::size_t, std::size_t>(sims[sim].caps[sims[sim].consumed].startByte,
                                                       sims[sim].pattern);
        };
        const auto drain = [&] {
            for (;;) {
                std::size_t best = static_cast<std::size_t>(-1);
                for (std::size_t index : finished) {
                    if (best == static_cast<std::size_t>(-1) || priority(index) < priority(best)) {
                        best = index;
                    }
                }
                if (best == static_cast<std::size_t>(-1)) {
                    return;
                }
                const auto bestPriority = priority(best);
                for (std::size_t index : active) {
                    if (!(bestPriority < priority(index))) {
                        return; // an in-progress state may still yield an earlier capture
                    }
                }
                SimMatch&           sim     = sims[best];
                const QueryCapture& capture = sim.caps[sim.consumed++];
                // Range filtering happens at emission, not formation --
                // measured: c's tags match spanning the range emits its
                // in-range capture while its before-range @name is silently
                // consumed.
                const bool inRange = capture.startByte == capture.endByte
                                         ? (capture.startByte >= rangeStart && capture.startByte < rangeEnd)
                                         : (capture.startByte < rangeEnd && capture.endByte > rangeStart);
                if (inRange) {
                    out.push_back(capture);
                }
                if (sim.consumed == sim.caps.size()) {
                    std::erase(finished, best);
                }
            }
        };

        for (const Event& event : events) {
            if (!event.isFinish) {
                active.push_back(event.sim);
            }
            else {
                std::erase(active, event.sim);
                finished.push_back(event.sim);
            }
            drain();
        }
        return out;
    }

    std::vector<QueryMatch> CollectMatches(parse::RedNode root, std::string_view text, std::size_t walkStart,
                                           std::size_t walkEnd) const {
        sourceText = text;
        // Unlike CollectCaptures, matches are never capture-filtered by the
        // range: the walk bound prunes which pattern ROOTS are tried, and a
        // root that intersects the range emits its whole match -- exactly
        // what a windowed symbol query needs (an enclosing definition's
        // @name may sit far above the window).
        rangeStart = 0;
        rangeEnd   = static_cast<std::size_t>(-1);
        std::vector<QueryMatch> matches;
        auto                    sink = [&](std::size_t patternIndex, const std::vector<Binding>& bindings) {
            QueryMatch match;
            match.captures.reserve(bindings.size());
            for (const Binding& binding : bindings) {
                match.captures.push_back(QueryMatchCapture{
                    .name      = captureNames[binding.captureId],
                    .startByte = parse::NodeStartByte(binding.node),
                    .endByte   = parse::NodeEndByte(binding.node),
                });
            }
            match.setDirectives    = SetDirectives(patternIndex, bindings);
            match.ancestorCrossing = patterns[patternIndex].readsOutsideSubtree;
            matches.push_back(std::move(match));
        };
        Walk(root, walkStart, walkEnd, sink);
        return matches;
    }
};

QueryMatcher::QueryMatcher(const Language& language, std::span<const querydata::Form> forms) : impl_(std::make_unique<Impl>()) {
    impl_->language = reinterpret_cast<const parse::abi::LanguageData*>(language.Raw());
    impl_->BuildTypeTables();
    impl_->CompileTopLevel(forms);
}

QueryMatcher::QueryMatcher(const Language& language, std::string_view source) : QueryMatcher(language, std::span<const Form>(querydata::ParseScm(source))) {
}

QueryMatcher::~QueryMatcher()                                        = default;
QueryMatcher::QueryMatcher(QueryMatcher&& other) noexcept            = default;
QueryMatcher& QueryMatcher::operator=(QueryMatcher&& other) noexcept = default;

std::vector<QueryCapture> QueryMatcher::Captures(const Node& root, std::string_view sourceText) const {
    return impl_->CollectCaptures(root.Raw(), sourceText, 0, static_cast<std::size_t>(-1));
}

std::vector<QueryCapture> QueryMatcher::CapturesInRange(const Node& root, std::string_view sourceText,
                                                        std::size_t startByte, std::size_t endByte) const {
    if (startByte >= endByte) {
        return {};
    }
    return impl_->CollectCaptures(root.Raw(), sourceText, startByte, endByte);
}

std::vector<QueryMatch> QueryMatcher::Matches(const Node& root, std::string_view sourceText) const {
    return impl_->CollectMatches(root.Raw(), sourceText, 0, static_cast<std::size_t>(-1));
}

std::vector<QueryMatch> QueryMatcher::MatchesInRange(const Node& root, std::string_view sourceText,
                                                     std::size_t startByte, std::size_t endByte) const {
    if (startByte >= endByte) {
        return {};
    }
    return impl_->CollectMatches(root.Raw(), sourceText, startByte, endByte);
}

std::size_t QueryMatcher::AncestorCrossingPatternCount() const {
    return static_cast<std::size_t>(
        std::count_if(impl_->patterns.begin(), impl_->patterns.end(),
                      [](const Pattern& pattern) { return pattern.readsOutsideSubtree; }));
}

} // namespace ned::editor::treesitter
