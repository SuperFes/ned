#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Mode.h"
#include "Editor/StickyScroll.h"

using ned::editor::CppMode;
using ned::editor::SymbolKind;
using ned::editor::SymbolMarker;
using ned::editor::stickyscroll::EnclosingSymbolChain;
using ned::editor::stickyscroll::StickyChainForViewportTop;

namespace {

std::vector<std::string> NamesInOrder(const std::vector<SymbolMarker>& chain) {
    std::vector<std::string> names;
    names.reserve(chain.size());
    for (const SymbolMarker& marker : chain) {
        names.push_back(marker.name);
    }
    return names;
}

} // namespace

TEST_CASE("EnclosingSymbolChain returns the outer-to-inner chain containing a point", "[StickyScroll]") {
    const auto mode = CppMode();
    const std::string source = "namespace outer {\nclass Widget {\n    void run() {\n        return;\n    }\n};\n}\n";
    const auto markers = mode.symbolKind(source);
    REQUIRE(markers.size() == 3); // outer, Widget, run

    // A point inside run()'s body (the "return;" line).
    const std::size_t point = source.find("return;");
    REQUIRE(NamesInOrder(EnclosingSymbolChain(markers, point)) == std::vector<std::string>{"outer", "Widget", "run"});
}

TEST_CASE("EnclosingSymbolChain excludes a sibling and anything past the point", "[StickyScroll]") {
    const auto mode = CppMode();
    const std::string source = "void a() {\n    1;\n}\nvoid b() {\n    2;\n}\n";
    const auto markers = mode.symbolKind(source);
    REQUIRE(markers.size() == 2);

    const std::size_t pointInA = source.find("1;");
    REQUIRE(NamesInOrder(EnclosingSymbolChain(markers, pointInA)) == std::vector<std::string>{"a"});

    // Past the end of both functions' ranges entirely.
    const std::size_t pointPastEverything = source.size();
    REQUIRE(EnclosingSymbolChain(markers, pointPastEverything).empty());
}

TEST_CASE("StickyChainForViewportTop excludes a header that's still the visible top line", "[StickyScroll]") {
    const auto mode = CppMode();
    const std::string source = "class Widget {\n    void run() {\n        return;\n    }\n};\n";
    const auto markers = mode.symbolKind(source);
    REQUIRE(markers.size() == 2); // Widget, run

    // Viewport top sits exactly at "class Widget {" -- its own header line
    // is fully visible, nothing has scrolled away, so the chain is empty.
    const std::size_t classLineStart = 0;
    REQUIRE(StickyChainForViewportTop(markers, classLineStart).empty());

    // Viewport top scrolled down to "void run() {" -- Widget's header has
    // scrolled off (sticky), but run()'s own header is the visible top line
    // (not sticky yet).
    const std::size_t runLineStart = source.find("void run()");
    REQUIRE(NamesInOrder(StickyChainForViewportTop(markers, runLineStart)) == std::vector<std::string>{"Widget"});

    // Viewport top scrolled past run()'s header too, into its body.
    const std::size_t returnLineStart = source.find("return;");
    REQUIRE(NamesInOrder(StickyChainForViewportTop(markers, returnLineStart)) == std::vector<std::string>{"Widget", "run"});
}

TEST_CASE("StickyChainForViewportTop is empty once the viewport scrolls past every enclosing block", "[StickyScroll]") {
    const auto mode = CppMode();
    const std::string source = "class Widget {\n    void run() {\n        return;\n    }\n};\nint after;\n";
    const auto markers = mode.symbolKind(source);

    const std::size_t afterLineStart = source.find("int after;");
    REQUIRE(StickyChainForViewportTop(markers, afterLineStart).empty());
}
