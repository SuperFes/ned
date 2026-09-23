#include "ImprintFold.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "Editor/ImprintBracket.h"
#include "Editor/ImprintTables.h"
#include "Editor/Grammar/IncrementalParse.h"
#include "Editor/Grammar/Languages.h"
#include "Editor/Grammar/Node.h"
#include "Editor/Grammar/Parser.h"

namespace ned::editor::imprint {

namespace {

    // One node's foldability, which both the node's own decision and its
    // parent's SupersededByChildBody check need.
    struct Candidate {
        const DelimitedBody* body  = nullptr; // null: not a foldable type at all
        std::size_t          start = 0;       // the anchored start; the node's own otherwise
    };

    Candidate CandidateFor(const grammar::Node& node, const ImprintTable& table, const FoldPolicy& policy,
                           std::string_view text) {
        const auto entry = table.find(node.Type());
        // The table answers for the node TYPE; a bracket body still has to
        // carry its brackets in this instance. Kotlin's `function_body` is
        // `{ ... }` OR `= expr`, and a multi-line expression body was being
        // offered as a fold with nothing to collapse -- see
        // `Editor/ImprintBracket.h`'s DelimitersOf for the two shapes this
        // rules out; a keyword body (`if ... fi`) is checked the same way
        // against the pair the table recorded. An indentation body is
        // exempt: its closer is a dedent, which is not a token at all.
        if (entry == table.end() || !ShouldFold(entry->second, policy) ||
            (entry->second.kind != DelimiterKind::Indent && !DelimitersOf(node, entry->second).has_value())) {
            return Candidate{nullptr, node.StartByte()};
        }
        return Candidate{&entry->second, FoldAnchorStart(entry->second, node.StartByte(), text)};
    }

    // One frame per open depth, reused across siblings so a node's child list
    // keeps its capacity rather than reallocating per foldable node.
    struct Frame {
        Candidate              candidate;
        std::size_t            endByte = 0;
        std::vector<ChildBody> children;
    };

    void Collect(const grammar::Node& root, const ImprintTable& table, const FoldPolicy& policy,
                 std::string_view text, std::vector<std::pair<std::size_t, std::size_t>>& out) {
        std::vector<Frame> frames;
        root.WalkSubtree(
            [&](const grammar::Node& node, std::size_t depth) {
                if (frames.size() <= depth) {
                    frames.resize(depth + 1);
                }
                Frame& frame    = frames[depth];
                frame.candidate = CandidateFor(node, table, policy, text);
                frame.endByte   = node.EndByte();
                frame.children.clear();
                if (depth > 0 && frames[depth - 1].candidate.body != nullptr) {
                    frames[depth - 1].children.push_back(
                        ChildBody{frame.candidate.body != nullptr, frame.candidate.start, frame.endByte});
                }
            },
            [&](const grammar::Node&, std::size_t depth) {
                const Frame& frame = frames[depth];
                if (frame.candidate.body != nullptr &&
                    !SupersededByChildBody(*frame.candidate.body, frame.candidate.start, frame.endByte, frame.children, text)) {
                    out.emplace_back(frame.candidate.start, frame.endByte);
                }
            });
    }

} // namespace

std::vector<std::pair<std::size_t, std::size_t>> CollectFoldBlocks(const grammar::Node& root,
                                                                   std::string_view language, std::string_view text,
                                                                   FoldPolicy policy) {
    const auto& table = TableFor(language);
    if (table.empty() || root.IsNull()) {
        return {};
    }
    std::vector<std::pair<std::size_t, std::size_t>> blocks;
    Collect(root, table, policy, text, blocks);
    return blocks;
}

FoldFunction MergeFoldSources(std::vector<FoldFunction> sources) {
    std::erase_if(sources, [](const FoldFunction& source) { return !source; });
    if (sources.empty()) return {};
    if (sources.size() == 1) return std::move(sources.front());

    return [sources = std::move(sources)](std::string_view bufferText) {
        std::vector<std::pair<std::size_t, std::size_t>> merged;
        for (const FoldFunction& source : sources) {
            const auto blocks = source(bufferText);
            merged.insert(merged.end(), blocks.begin(), blocks.end());
        }
        std::sort(merged.begin(), merged.end());
        merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
        return merged;
    };
}

FoldFunction BuildFoldFunction(std::string_view language, FoldPolicy policy) {
    const auto& table = TableFor(language);
    if (table.empty()) {
        return {};
    }
    const std::optional<grammar::Language> resolved = grammar::LanguageByName(language);
    if (!resolved.has_value()) {
        return {};
    }

    // One parser and one incremental cache per constructed function, captured
    // by value in a shared_ptr -- the same idiom every bundled Mode's own
    // tree-sitter closure uses (see Mode.cpp), and the reason a Mode stays a
    // freely copyable value type.
    auto parser = std::make_shared<grammar::Parser>(*resolved);
    auto cache  = std::make_shared<grammar::IncrementalParseCache>();

    // `table` is captured by reference deliberately and safely: TableFor hands
    // back a reference to a function-local static, so it outlives every
    // closure built from it. Copying it per Mode would duplicate a few hundred
    // entries for every buffer opened.

    return [parser, cache, &table, policy](std::string_view bufferText) {
        const grammar::Tree&                         tree = cache->Update(*parser, bufferText);
        std::vector<std::pair<std::size_t, std::size_t>> blocks;
        if (!tree.IsNull())
            Collect(tree.RootNode(), table, policy, bufferText, blocks);
        return blocks;
    };
}

} // namespace ned::editor::imprint
