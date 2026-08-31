#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>

#include "Text/ITextStorage.h"
#include "Text/Rope.h"
#include "Text/RopeStorage.h"
#include "Text/UndoTree.h"

using ned::text::ITextStorage;
using ned::text::Rope;
using ned::text::RopeStorage;
using ned::text::UndoTree;

namespace {
std::unique_ptr<ITextStorage> Snapshot(const char* text) {
    return std::make_unique<RopeStorage>(Rope(text));
}
} // namespace

TEST_CASE("Fresh UndoTree has no undo/redo history", "[UndoTree]") {
    UndoTree tree(Snapshot("start"));

    REQUIRE(tree.Current().ToString() == "start");
    REQUIRE_FALSE(tree.CanUndo());
    REQUIRE_FALSE(tree.CanRedo());
}

TEST_CASE("Linear undo/redo walks recorded states in order", "[UndoTree]") {
    UndoTree tree(Snapshot("start"));

    tree.Record(Snapshot("start+1"));
    tree.Record(Snapshot("start+2"));
    REQUIRE(tree.Current().ToString() == "start+2");

    tree.Undo();
    REQUIRE(tree.Current().ToString() == "start+1");

    tree.Undo();
    REQUIRE(tree.Current().ToString() == "start");
    REQUIRE_FALSE(tree.CanUndo());

    tree.Redo();
    REQUIRE(tree.Current().ToString() == "start+1");

    tree.Redo();
    REQUIRE(tree.Current().ToString() == "start+2");
    REQUIRE_FALSE(tree.CanRedo());
}

TEST_CASE("A new edit after undo branches instead of discarding history", "[UndoTree]") {
    UndoTree tree(Snapshot("start"));

    tree.Record(Snapshot("start+1")); // branch A
    tree.Undo();
    REQUIRE(tree.Current().ToString() == "start");
    REQUIRE(tree.ChildCount() == 1);

    tree.Record(Snapshot("start+A")); // branch B, sibling of branch A
    REQUIRE(tree.Current().ToString() == "start+A");

    tree.Undo();
    REQUIRE(tree.Current().ToString() == "start");
    REQUIRE(tree.ChildCount() == 2); // both branches still present

    // Redo follows the most-recently-visited child (branch B), not branch A.
    tree.Redo();
    REQUIRE(tree.Current().ToString() == "start+A");
}

TEST_CASE("Amend replaces the current step instead of creating a new one", "[UndoTree]") {
    UndoTree tree(Snapshot(""));

    tree.Record(Snapshot("a"));
    tree.Amend(Snapshot("ab"));
    tree.Amend(Snapshot("abc"));

    REQUIRE(tree.Current().ToString() == "abc");

    tree.Undo();
    REQUIRE(tree.Current().ToString() == ""); // amends collapsed into the one step
    REQUIRE_FALSE(tree.CanUndo());
}

TEST_CASE("Serialize/Deserialize round-trips a linear tree", "[UndoTree]") {
    UndoTree tree(Snapshot("start"));
    tree.Record(Snapshot("start+1"));
    tree.Record(Snapshot("start+2"));
    tree.Undo();

    const auto        nodes     = tree.Serialize();
    const std::size_t currentId = tree.CurrentNodeId();
    REQUIRE(nodes.size() == 3);

    UndoTree restored = UndoTree::Deserialize(nodes, currentId);
    REQUIRE(restored.Current().ToString() == "start+1");
    REQUIRE(restored.CanUndo());
    REQUIRE(restored.CanRedo());

    restored.Redo();
    REQUIRE(restored.Current().ToString() == "start+2");
    restored.Undo();
    restored.Undo();
    REQUIRE(restored.Current().ToString() == "start");
    REQUIRE_FALSE(restored.CanUndo());
}

TEST_CASE("Serialize/Deserialize round-trips branches and redo choice", "[UndoTree]") {
    UndoTree tree(Snapshot("start"));
    tree.Record(Snapshot("branch-A"));
    tree.Undo();
    tree.Record(Snapshot("branch-B")); // most-recently-visited child of root

    const auto        nodes     = tree.Serialize();
    const std::size_t currentId = tree.CurrentNodeId();
    REQUIRE(nodes.size() == 3);

    UndoTree restored = UndoTree::Deserialize(nodes, currentId);
    REQUIRE(restored.Current().ToString() == "branch-B");
    restored.Undo();
    REQUIRE(restored.ChildCount() == 2);
    restored.Redo();
    REQUIRE(restored.Current().ToString() == "branch-B"); // mostRecentChild preserved
}

TEST_CASE("Deserialize rejects malformed node lists", "[UndoTree]") {
    using Node = UndoTree::SerializedNode;

    REQUIRE_THROWS_AS(UndoTree::Deserialize({}, 0), std::runtime_error); // empty
    REQUIRE_THROWS_AS(UndoTree::Deserialize({Node{0, std::nullopt, "a", 0}}, 1),
                      std::runtime_error); // unknown currentId
    REQUIRE_THROWS_AS(UndoTree::Deserialize({Node{0, std::nullopt, "a", 0}, Node{1, std::nullopt, "b", 0}}, 0),
                      std::runtime_error);                                                 // two roots
    REQUIRE_THROWS_AS(UndoTree::Deserialize({Node{0, 1, "a", 0}}, 0), std::runtime_error); // no root at all
    REQUIRE_THROWS_AS(UndoTree::Deserialize({Node{0, std::nullopt, "a", 0}, Node{1, 2, "b", 0}}, 0),
                      std::runtime_error); // dangling parentId
    REQUIRE_THROWS_AS(UndoTree::Deserialize({Node{0, 1, "a", 0}, Node{1, 0, "b", 0}}, 0),
                      std::runtime_error); // 2-cycle, no root reachable
    REQUIRE_THROWS_AS(UndoTree::Deserialize({Node{0, std::nullopt, "a", 0}, Node{0, std::nullopt, "b", 0}}, 0),
                      std::runtime_error); // duplicate id
}
