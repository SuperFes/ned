#include "ImprintFold.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "Editor/ImprintBracket.h"
#include "Editor/ImprintTables.h"
#include "Editor/TreeSitter/IncrementalParse.h"
#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/Node.h"
#include "Editor/TreeSitter/Parser.h"

namespace ned::editor::imprint {

namespace {

    void Collect(const treesitter::Node& node, const std::map<std::string, DelimitedBody>& table,
                 const FoldPolicy& policy, std::string_view text,
                 std::vector<std::pair<std::size_t, std::size_t>>& out) {
        if (node.IsNull())
            return;

        const auto entry = table.find(std::string(node.Type()));
        // The table answers for the node TYPE; a bracket body still has to
        // carry its brackets in this instance. Kotlin's `function_body` is
        // `{ ... }` OR `= expr`, and a multi-line expression body was being
        // offered as a fold with nothing to collapse -- see
        // `Editor/ImprintBracket.h`'s DelimitersOf for the two shapes this
        // rules out; a keyword body (`if ... fi`) is checked the same way
        // against the pair the table recorded. An indentation body is
        // exempt: its closer is a dedent, which is not a token at all.
        const bool        foldable   = entry != table.end() && ShouldFold(entry->second, policy) &&
                                       (entry->second.kind == DelimiterKind::Indent ||
                                        DelimitersOf(node, entry->second).has_value());
        const std::size_t childCount = node.ChildCount();

        if (foldable) {
            const std::size_t      start = FoldAnchorStart(entry->second, node.StartByte(), text);
            std::vector<ChildBody> children;
            children.reserve(childCount);
            for (std::size_t i = 0; i < childCount; ++i) {
                const treesitter::Node child = node.Child(i);
                const auto             found = table.find(std::string(child.Type()));
                const bool             childFolds =
                    found != table.end() && ShouldFold(found->second, policy) &&
                    (found->second.kind == DelimiterKind::Indent || DelimitersOf(child, found->second).has_value());
                children.push_back(ChildBody{childFolds,
                                             childFolds ? FoldAnchorStart(found->second, child.StartByte(), text)
                                                        : child.StartByte(),
                                             child.EndByte()});
            }
            if (!SupersededByChildBody(entry->second, start, node.EndByte(), children, text)) {
                out.emplace_back(start, node.EndByte());
            }
        }

        for (std::size_t i = 0; i < childCount; ++i)
            Collect(node.Child(i), table, policy, text, out);
    }

} // namespace

std::vector<std::pair<std::size_t, std::size_t>> CollectFoldBlocks(const treesitter::Node& root,
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
    const std::optional<treesitter::Language> resolved = treesitter::LanguageByName(language);
    if (!resolved.has_value()) {
        return {};
    }

    // One parser and one incremental cache per constructed function, captured
    // by value in a shared_ptr -- the same idiom every bundled Mode's own
    // tree-sitter closure uses (see Mode.cpp), and the reason a Mode stays a
    // freely copyable value type.
    auto parser = std::make_shared<treesitter::Parser>(*resolved);
    auto cache  = std::make_shared<treesitter::IncrementalParseCache>();

    // `table` is captured by reference deliberately and safely: TableFor hands
    // back a reference to a function-local static, so it outlives every
    // closure built from it. Copying it per Mode would duplicate a few hundred
    // entries for every buffer opened.

    return [parser, cache, &table, policy](std::string_view bufferText) {
        const treesitter::Tree&                         tree = cache->Update(*parser, bufferText);
        std::vector<std::pair<std::size_t, std::size_t>> blocks;
        if (!tree.IsNull())
            Collect(tree.RootNode(), table, policy, bufferText, blocks);
        return blocks;
    };
}

} // namespace ned::editor::imprint
