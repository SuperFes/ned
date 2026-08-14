#include <catch2/catch_test_macros.hpp>

#include "Text/Rope.h"
#include "Text/UndoTree.h"

using ned::text::Rope;
using ned::text::UndoTree;

TEST_CASE("Fresh UndoTree has no undo/redo history", "[UndoTree]") {
    UndoTree tree(Rope("start"));

    REQUIRE(tree.Current().ToString() == "start");
    REQUIRE_FALSE(tree.CanUndo());
    REQUIRE_FALSE(tree.CanRedo());
}

TEST_CASE("Linear undo/redo walks recorded states in order", "[UndoTree]") {
    UndoTree tree(Rope("start"));

    tree.Record(Rope("start+1"));
    tree.Record(Rope("start+2"));
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
    UndoTree tree(Rope("start"));

    tree.Record(Rope("start+1")); // branch A
    tree.Undo();
    REQUIRE(tree.Current().ToString() == "start");
    REQUIRE(tree.ChildCount() == 1);

    tree.Record(Rope("start+A")); // branch B, sibling of branch A
    REQUIRE(tree.Current().ToString() == "start+A");

    tree.Undo();
    REQUIRE(tree.Current().ToString() == "start");
    REQUIRE(tree.ChildCount() == 2); // both branches still present

    // Redo follows the most-recently-visited child (branch B), not branch A.
    tree.Redo();
    REQUIRE(tree.Current().ToString() == "start+A");
}

TEST_CASE("Amend replaces the current step instead of creating a new one", "[UndoTree]") {
    UndoTree tree(Rope(""));

    tree.Record(Rope("a"));
    tree.Amend(Rope("ab"));
    tree.Amend(Rope("abc"));

    REQUIRE(tree.Current().ToString() == "abc");

    tree.Undo();
    REQUIRE(tree.Current().ToString() == ""); // amends collapsed into the one step
    REQUIRE_FALSE(tree.CanUndo());
}
