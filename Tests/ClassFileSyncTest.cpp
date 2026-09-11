//
// class-file-sync follow-up: Editor/ClassFileSync.h's pure half -- the
// containment rule that decides what a file's "one top-level type" is, the
// first-dot filename split that keeps a compound suffix through a stem swap,
// and the two automatic-offer predicates.
//
// Everything here builds SymbolMarker vectors by hand rather than parsing:
// the real grammars are exercised in ModeTest.cpp, and the questions this
// module answers (what encloses what, which name wins) are about the marker
// list itself, not about any language.
//

#include "Editor/ClassFileSync.h"

#include <catch2/catch_test_macros.hpp>

using ned::editor::SymbolKind;
using ned::editor::SymbolMarker;
using namespace ned::editor::classfile;

namespace {

SymbolMarker Marker(std::size_t start, std::size_t end, SymbolKind kind, std::string name) {
    return SymbolMarker{.startByte = start, .endByte = end, .kind = kind, .name = std::move(name)};
}

std::vector<std::string> NamesOf(const std::vector<TypeMatch>& types) {
    std::vector<std::string> names;
    names.reserve(types.size());
    for (const TypeMatch& type : types) {
        names.push_back(type.name);
    }
    return names;
}

} // namespace

TEST_CASE("SplitFileName splits at the first dot, so a compound suffix survives a stem swap", "[ClassFileSync]") {
    REQUIRE(SplitFileName("Widget.php").stem == "Widget");
    REQUIRE(SplitFileName("Widget.php").suffix == ".php");

    // The PEAR-era spelling still in the wild, and the modern spellings of
    // the same idea -- one rule covers all of them, no suffix table.
    REQUIRE(SplitFileName("Thing.class.php").stem == "Thing");
    REQUIRE(SplitFileName("Thing.class.php").suffix == ".class.php");
    REQUIRE(SplitFileName("types.d.ts").suffix == ".d.ts");
    REQUIRE(SplitFileName("Widget.test.tsx").stem == "Widget");
    REQUIRE(SplitFileName("Widget.test.tsx").suffix == ".test.tsx");

    // No dot at all, and a dotfile whose leading dot is part of its name.
    REQUIRE(SplitFileName("Makefile").stem == "Makefile");
    REQUIRE(SplitFileName("Makefile").suffix.empty());
    REQUIRE(SplitFileName(".gitignore").stem == ".gitignore");
    REQUIRE(SplitFileName(".gitignore").suffix.empty());
    REQUIRE(SplitFileName("").stem.empty());
}

TEST_CASE("FileNameForType swaps the stem and keeps the suffix verbatim", "[ClassFileSync]") {
    REQUIRE(FileNameForType("Widget.php", "Gadget") == "Gadget.php");
    REQUIRE(FileNameForType("Thing.class.php", "Widget") == "Widget.class.php");
    REQUIRE(FileNameForType("Widget.test.tsx", "Gadget") == "Gadget.test.tsx");
    REQUIRE(FileNameForType("Makefile", "Widget") == "Widget");

    // Anything that could not be a filename declines rather than producing
    // something odd -- the caller reports it instead of writing a path.
    REQUIRE(FileNameForType("Widget.php", "").empty());
    REQUIRE(FileNameForType("Widget.php", "App/Widget").empty());
    REQUIRE(FileNameForType("Widget.php", "..").empty());
}

TEST_CASE("TopLevelTypes ignores a nested type but keeps a namespaced one", "[ClassFileSync]") {
    // class Outer { class Inner {} } -- only Outer is the file's type.
    const std::vector<SymbolMarker> nested = {Marker(0, 100, SymbolKind::TypeLike, "Outer"),
                                              Marker(20, 60, SymbolKind::TypeLike, "Inner")};
    REQUIRE(NamesOf(TopLevelTypes(nested)) == std::vector<std::string>{"Outer"});

    // namespace App { class Widget {} } -- the block form wraps the class, so
    // counting a namespace as a container would hide every namespaced type.
    const std::vector<SymbolMarker> wrapped = {Marker(0, 100, SymbolKind::Namespace, "App"),
                                               Marker(20, 90, SymbolKind::TypeLike, "Widget")};
    REQUIRE(NamesOf(TopLevelTypes(wrapped)) == std::vector<std::string>{"Widget"});

    // namespace App; class Widget {} -- PHP's statement form and C# 10's
    // file-scoped form both sit BESIDE the type rather than around it.
    // Counting a namespace as a type would report two here; skipping it for
    // both purposes is what makes the two spellings behave identically.
    const std::vector<SymbolMarker> beside = {Marker(0, 14, SymbolKind::Namespace, "App"),
                                              Marker(16, 90, SymbolKind::TypeLike, "Widget")};
    REQUIRE(NamesOf(TopLevelTypes(beside)) == std::vector<std::string>{"Widget"});
}

TEST_CASE("TopLevelTypes treats a function as a container, so a class inside one doesn't count", "[ClassFileSync]") {
    // function factory() { class Inner {} } -- ordinary JavaScript, and
    // plainly not the file's type.
    const std::vector<SymbolMarker> markers = {Marker(0, 100, SymbolKind::Callable, "factory"),
                                               Marker(20, 60, SymbolKind::TypeLike, "Inner")};
    REQUIRE(TopLevelTypes(markers).empty());
}

TEST_CASE("TopLevelTypes reports several siblings in source order", "[ClassFileSync]") {
    const std::vector<SymbolMarker> markers = {Marker(0, 40, SymbolKind::TypeLike, "First"),
                                               Marker(50, 90, SymbolKind::TypeLike, "Second"),
                                               Marker(60, 80, SymbolKind::Callable, "method")};
    REQUIRE(NamesOf(TopLevelTypes(markers)) == std::vector<std::string>{"First", "Second"});
}

TEST_CASE("TopLevelTypes skips a marker with no captured name and an exact duplicate range", "[ClassFileSync]") {
    // A tags.scm match with no @name capture cannot be renamed to or from,
    // so it is not a candidate -- Mode.h says name can be empty and never
    // assumes otherwise.
    REQUIRE(TopLevelTypes(std::vector<SymbolMarker>{Marker(0, 40, SymbolKind::TypeLike, "")}).empty());

    // Two patterns tagging the identical range (which Mode.cpp already
    // dedupes, but this must not mistake for containment and drop both).
    const std::vector<SymbolMarker> duplicate = {Marker(0, 40, SymbolKind::TypeLike, "Widget"),
                                                 Marker(0, 40, SymbolKind::TypeLike, "Widget")};
    REQUIRE(NamesOf(TopLevelTypes(duplicate)) == std::vector<std::string>{"Widget", "Widget"});
}

TEST_CASE("SelectType's strict tier refuses several types where best-effort takes the outermost",
          "[ClassFileSync]") {
    const std::vector<SymbolMarker> two = {Marker(0, 40, SymbolKind::TypeLike, "First"),
                                           Marker(50, 90, SymbolKind::TypeLike, "Second")};

    const Resolution strict = SelectType(two, Strictness::Strict);
    REQUIRE_FALSE(strict.type.has_value());
    REQUIRE(strict.decline == Decline::SeveralTypes);
    REQUIRE(strict.candidateCount == 2);

    // Best effort still reports the count, so the caller can name which one
    // it picked instead of silently acting on one of several.
    const Resolution loose = SelectType(two, Strictness::BestEffort);
    REQUIRE(loose.type.has_value());
    REQUIRE(loose.type->name == "First");
    REQUIRE(loose.decline == Decline::None);
    REQUIRE(loose.candidateCount == 2);
}

TEST_CASE("SelectType reports NoTypes for an empty marker list under either tier", "[ClassFileSync]") {
    for (const Strictness strictness : {Strictness::Strict, Strictness::BestEffort}) {
        const Resolution resolution = SelectType(std::vector<SymbolMarker>{}, strictness);
        REQUIRE_FALSE(resolution.type.has_value());
        REQUIRE(resolution.decline == Decline::NoTypes);
        REQUIRE(resolution.candidateCount == 0);
    }
}

TEST_CASE("FileRenameFollowsSymbolRename fires only when the file was already named after the renamed type",
          "[ClassFileSync]") {
    const std::vector<SymbolMarker> markers  = {Marker(0, 90, SymbolKind::TypeLike, "Gadget")};
    const Resolution                resolved = SelectType(markers, Strictness::Strict);

    // Widget.php held class Widget, which just became Gadget.
    REQUIRE(FileRenameFollowsSymbolRename(resolved, "Widget.php", "Widget", "Gadget"));
    REQUIRE(FileRenameFollowsSymbolRename(resolved, "Widget.class.php", "Widget", "Gadget"));

    // The evidence-not-convention rule: Helpers.php deliberately holds class
    // Gadget, so renaming it is not a reason to rename the file.
    REQUIRE_FALSE(FileRenameFollowsSymbolRename(resolved, "Helpers.php", "Widget", "Gadget"));

    // A rename of something else in the same file must not drag the filename
    // along -- the renamed symbol has to BE the file's one type.
    REQUIRE_FALSE(FileRenameFollowsSymbolRename(resolved, "Widget.php", "count", "total"));

    // Degenerate renames.
    REQUIRE_FALSE(FileRenameFollowsSymbolRename(resolved, "Widget.php", "Widget", "Widget"));
    REQUIRE_FALSE(FileRenameFollowsSymbolRename(resolved, "Widget.php", "", "Gadget"));
}

TEST_CASE("FileRenameFollowsSymbolRename stays silent when the file holds more than one type",
          "[ClassFileSync]") {
    const std::vector<SymbolMarker> two = {Marker(0, 40, SymbolKind::TypeLike, "Gadget"),
                                           Marker(50, 90, SymbolKind::TypeLike, "Helper")};
    REQUIRE_FALSE(FileRenameFollowsSymbolRename(SelectType(two, Strictness::Strict), "Widget.php", "Widget",
                                                "Gadget"));
    // Even the permissive tier's own pick does not license the automatic
    // offer -- candidateCount, not the tier, is what gates it.
    REQUIRE_FALSE(FileRenameFollowsSymbolRename(SelectType(two, Strictness::BestEffort), "Widget.php", "Widget",
                                                "Gadget"));
}

TEST_CASE("TypeRenameFollowsFileMove is the mirror rule, and declines a stem no language would accept",
          "[ClassFileSync]") {
    const std::vector<SymbolMarker> markers  = {Marker(0, 90, SymbolKind::TypeLike, "Widget")};
    const Resolution                resolved = SelectType(markers, Strictness::Strict);

    REQUIRE(TypeRenameFollowsFileMove(resolved, "Widget.php", "Gadget.php"));
    REQUIRE(TypeRenameFollowsFileMove(resolved, "Widget.class.php", "Gadget.class.php"));

    // A pure directory move changes no stem, so there is nothing to rename.
    REQUIRE_FALSE(TypeRenameFollowsFileMove(resolved, "Widget.php", "Widget.php"));

    // The file was never named after this type.
    REQUIRE_FALSE(TypeRenameFollowsFileMove(resolved, "Helpers.php", "Utilities.php"));

    // Real, ordinary filenames that simply are not type names.
    REQUIRE_FALSE(TypeRenameFollowsFileMove(resolved, "Widget.php", "my-component.php"));
    REQUIRE_FALSE(TypeRenameFollowsFileMove(resolved, "Widget.php", "02_migration.php"));
}

TEST_CASE("LooksLikeTypeName accepts identifiers and non-ASCII, rejects punctuation and a leading digit",
          "[ClassFileSync]") {
    REQUIRE(LooksLikeTypeName("Widget"));
    REQUIRE(LooksLikeTypeName("_private"));
    REQUIRE(LooksLikeTypeName("Widget2"));
    REQUIRE(LooksLikeTypeName("Grösse")); // every byte >= 0x80 passes, same rule RenameReview.h takes

    REQUIRE_FALSE(LooksLikeTypeName(""));
    REQUIRE_FALSE(LooksLikeTypeName("my-component"));
    REQUIRE_FALSE(LooksLikeTypeName("02_migration"));
    REQUIRE_FALSE(LooksLikeTypeName("has space"));
}
