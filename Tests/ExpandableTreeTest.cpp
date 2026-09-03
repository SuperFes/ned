#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/ExpandableTree.h"

using ned::editor::ExpandableTree;

namespace {

// A minimal NodeData for these tests -- the real consumer is
// ned::editor::lsp::HierarchyItem, but this file must not depend on Lsp/ to
// stay a pure test of the generic template.
struct Item {
    std::string name;
    bool operator==(const Item&) const = default;
};

} // namespace

TEST_CASE("Reset seeds roots with no parent, unexpanded and with children unfetched", "[ExpandableTree]") {
    ExpandableTree<Item> tree;
    tree.Reset({Item{"a"}, Item{"b"}});
    REQUIRE(tree.Size() == 2);
    REQUIRE_FALSE(tree.At(0).parent.has_value());
    REQUIRE_FALSE(tree.At(1).parent.has_value());
    REQUIRE_FALSE(tree.IsExpanded(0));
    REQUIRE_FALSE(tree.ChildrenFetched(0));
    REQUIRE_FALSE(tree.IsLoading(0));

    const auto rows = tree.FlattenVisible();
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].index == 0);
    REQUIRE(rows[0].depth == 0);
    REQUIRE(rows[1].index == 1);
    REQUIRE(rows[1].depth == 0);
}

TEST_CASE("BeginLoading sets IsLoading until Expand clears it", "[ExpandableTree]") {
    ExpandableTree<Item> tree;
    tree.Reset({Item{"root"}});
    tree.BeginLoading(0);
    REQUIRE(tree.IsLoading(0));

    tree.Expand(0, {Item{"child"}});
    REQUIRE_FALSE(tree.IsLoading(0));
    REQUIRE(tree.IsExpanded(0));
    REQUIRE(tree.ChildrenFetched(0));
}

TEST_CASE("Expand appends children parented to the expanded node and they appear in FlattenVisible", "[ExpandableTree]") {
    ExpandableTree<Item> tree;
    tree.Reset({Item{"root"}});
    tree.Expand(0, {Item{"child1"}, Item{"child2"}});

    REQUIRE(tree.Size() == 3);
    REQUIRE(tree.At(1).parent == 0);
    REQUIRE(tree.At(2).parent == 0);
    REQUIRE(tree.At(0).children == std::vector<std::size_t>{1, 2});

    const auto rows = tree.FlattenVisible();
    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0] == ExpandableTree<Item>::VisibleRow{0, 0});
    REQUIRE(rows[1] == ExpandableTree<Item>::VisibleRow{1, 1});
    REQUIRE(rows[2] == ExpandableTree<Item>::VisibleRow{2, 1});
}

TEST_CASE("Expand with an empty child list still marks childrenFetched (a real \"no children\" answer)",
          "[ExpandableTree]") {
    ExpandableTree<Item> tree;
    tree.Reset({Item{"leaf"}});
    tree.Expand(0, {});
    REQUIRE(tree.ChildrenFetched(0));
    REQUIRE(tree.IsExpanded(0));
    REQUIRE(tree.Size() == 1); // no children appended
    REQUIRE(tree.FlattenVisible().size() == 1);
}

TEST_CASE("Collapse hides a subtree from FlattenVisible without discarding fetched children", "[ExpandableTree]") {
    ExpandableTree<Item> tree;
    tree.Reset({Item{"root"}});
    tree.Expand(0, {Item{"child"}});
    REQUIRE(tree.FlattenVisible().size() == 2);

    tree.SetExpanded(0, false);
    REQUIRE_FALSE(tree.IsExpanded(0));
    REQUIRE(tree.ChildrenFetched(0)); // still fetched, just hidden
    REQUIRE(tree.Size() == 2);        // child node still exists
    REQUIRE(tree.FlattenVisible().size() == 1);

    // Re-expanding needs no re-fetch -- the same child reappears.
    tree.SetExpanded(0, true);
    const auto rows = tree.FlattenVisible();
    REQUIRE(rows.size() == 2);
    REQUIRE(tree.At(rows[1].index).data.name == "child");
}

TEST_CASE("Collapsing an ancestor hides its whole expanded subtree, not just its direct children", "[ExpandableTree]") {
    ExpandableTree<Item> tree;
    tree.Reset({Item{"root"}});
    tree.Expand(0, {Item{"child"}});
    tree.Expand(1, {Item{"grandchild"}});
    REQUIRE(tree.FlattenVisible().size() == 3);

    tree.SetExpanded(0, false);
    REQUIRE(tree.FlattenVisible().size() == 1); // child and grandchild both hidden

    tree.SetExpanded(0, true);
    REQUIRE(tree.FlattenVisible().size() == 3); // grandchild still expanded underneath, reappears too
}

TEST_CASE("SetExpanded(true) on a node with no fetched children is a harmless no-op", "[ExpandableTree]") {
    ExpandableTree<Item> tree;
    tree.Reset({Item{"root"}});
    tree.SetExpanded(0, true);
    REQUIRE(tree.IsExpanded(0));
    REQUIRE(tree.FlattenVisible().size() == 1); // nothing to reveal -- no children fetched yet
}

TEST_CASE("Multiple roots each flatten independently in Reset's own order", "[ExpandableTree]") {
    ExpandableTree<Item> tree;
    tree.Reset({Item{"a"}, Item{"b"}, Item{"c"}});
    tree.Expand(1, {Item{"b-child"}});

    const auto rows = tree.FlattenVisible();
    REQUIRE(rows.size() == 4);
    REQUIRE(tree.At(rows[0].index).data.name == "a");
    REQUIRE(tree.At(rows[1].index).data.name == "b");
    REQUIRE(tree.At(rows[2].index).data.name == "b-child");
    REQUIRE(rows[2].depth == 1);
    REQUIRE(tree.At(rows[3].index).data.name == "c");
}

TEST_CASE("Reset clears any prior session's nodes entirely", "[ExpandableTree]") {
    ExpandableTree<Item> tree;
    tree.Reset({Item{"old"}});
    tree.Expand(0, {Item{"old-child"}});
    REQUIRE(tree.Size() == 2);

    tree.Reset({Item{"new"}});
    REQUIRE(tree.Size() == 1);
    REQUIRE(tree.At(0).data.name == "new");
    REQUIRE_FALSE(tree.ChildrenFetched(0));
}
