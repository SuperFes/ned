#include "ImprintFold.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "Editor/ImprintTables.h"
#include "Editor/TreeSitter/IncrementalParse.h"
#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/Node.h"
#include "Editor/TreeSitter/Parser.h"

namespace ned::editor::imprint {

namespace {

void Collect(const treesitter::Node& node, const std::map<std::string, DelimitedBody>& table,
             const FoldPolicy& policy, std::vector<std::pair<std::size_t, std::size_t>>& out) {
    if (node.IsNull()) return;

    const auto entry     = table.find(std::string(node.Type()));
    const bool foldable  = entry != table.end() && ShouldFold(entry->second, policy);
    const std::size_t childCount = node.ChildCount();

    if (foldable) {
        std::vector<std::pair<bool, std::size_t>> children;
        children.reserve(childCount);
        for (std::size_t i = 0; i < childCount; ++i) {
            const treesitter::Node child = node.Child(i);
            const auto             found = table.find(std::string(child.Type()));
            children.emplace_back(found != table.end() && ShouldFold(found->second, policy), child.EndByte());
        }
        if (!SupersededByChildBody(node.EndByte(), children)) {
            out.emplace_back(node.StartByte(), node.EndByte());
        }
    }

    for (std::size_t i = 0; i < childCount; ++i) Collect(node.Child(i), table, policy, out);
}

} // namespace

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
        Collect(tree.RootNode(), table, policy, blocks);
        return blocks;
    };
}

} // namespace ned::editor::imprint
