#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/EmbeddedDocuments.h"
#include "Editor/Lsp/LspPosition.h"
#include "Editor/Mode.h"
#include "Text/Rope.h"
#include "Text/RopeStorage.h"

using namespace ned::editor;
using ned::editor::lsp::BytePositionToLsp;
using ned::text::Rope;
using ned::text::RopeStorage;

namespace {

std::size_t Find(const std::string& text, const std::string& needle) {
    const std::size_t offset = text.find(needle);
    REQUIRE(offset != std::string::npos);
    return offset;
}

} // namespace

TEST_CASE("BuildEmbeddedDocuments returns nothing for a mode with no embeddedRegions hook", "[EmbeddedDocuments]") {
    const Mode mode = FundamentalMode();
    REQUIRE(BuildEmbeddedDocuments(mode, "anything at all").empty());
}

TEST_CASE("BuildEmbeddedDocuments merges two same-language regions into one document", "[EmbeddedDocuments]") {
    const Mode        mode = HtmlMode();
    const std::string text = "<script>let a = 1;</script><div></div><script>let b = 2;</script>";

    const auto documents = BuildEmbeddedDocuments(mode, text);
    REQUIRE(documents.size() == 1);
    REQUIRE(documents[0].language == "javascript");
    REQUIRE(documents[0].ownedRanges.size() == 2);
    REQUIRE(documents[0].documentText.size() == text.size());
}

TEST_CASE("BuildEmbeddedDocuments' padded text preserves byte length and line count exactly", "[EmbeddedDocuments]") {
    const Mode        mode = HtmlMode();
    const std::string text = "<html>\n<script>\nlet x = 1;\n</script>\n<style>\nbody {}\n</style>\n</html>\n";

    const auto documents = BuildEmbeddedDocuments(mode, text);
    REQUIRE(documents.size() == 2);
    for (const auto& document : documents) {
        REQUIRE(document.documentText.size() == text.size());
        REQUIRE(Rope(document.documentText).LineCount() == Rope(text).LineCount());
    }
}

TEST_CASE("BuildEmbeddedDocuments keeps owned content verbatim and blanks everything else", "[EmbeddedDocuments]") {
    const Mode        mode = HtmlMode();
    const std::string text = "<div>markup</div><script>let x = 1;</script><div>more</div>";

    const auto documents = BuildEmbeddedDocuments(mode, text);
    REQUIRE(documents.size() == 1);
    const auto& js = documents[0];

    const std::size_t scriptContentStart = Find(text, "let x = 1;");
    REQUIRE(js.documentText.substr(scriptContentStart, std::string("let x = 1;").size()) == "let x = 1;");

    // Host markup outside the owned range must not appear in the padded
    // text -- the whole point of padding is that no real host-language
    // content is ever exposed to the embedded server.
    REQUIRE(js.documentText.find("markup") == std::string::npos);
    REQUIRE(js.documentText.find("more") == std::string::npos);
    REQUIRE(js.documentText.find("<div>") == std::string::npos);
}

TEST_CASE("BuildEmbeddedDocuments' width-preserving padding keeps LSP position math identical to the host buffer",
          "[EmbeddedDocuments]") {
    // A multi-byte UTF-8 character ("é", 2 bytes) sits in host markup on the
    // same line as, and before, a <script> tag -- this is exactly the case
    // naive byte-for-byte ASCII-space padding gets wrong (it would inflate
    // the UTF-16 character count on this line by turning one 2-byte
    // codepoint into two single-byte space codepoints). The critical
    // invariant: BytePositionToLsp computed against the padded virtual
    // document must equal the same computed against the real host buffer,
    // at every offset inside the owned (script) region.
    const Mode        mode = HtmlMode();
    const std::string text = "<!-- café -->\n<script>\nlet value = 42;\n</script>\n";

    const auto documents = BuildEmbeddedDocuments(mode, text);
    REQUIRE(documents.size() == 1);
    const auto& js = documents[0];

    const RopeStorage hostRope{Rope(text)};
    const RopeStorage virtualRope{Rope(js.documentText)};

    const std::size_t valueOffset = Find(text, "value");
    REQUIRE(BytePositionToLsp(virtualRope, valueOffset) == BytePositionToLsp(hostRope, valueOffset));

    // Also check the very end of the owned range and a boundary just past a
    // 4-byte (astral) codepoint, exercising the two-NBSP filler path.
    const std::string astral     = "\xF0\x9F\x98\x80"; // U+1F600, 4 bytes
    const std::string text2      = "<!-- " + astral + " -->\n<script>\nlet after = 1;\n</script>\n";
    const auto        documents2 = BuildEmbeddedDocuments(mode, text2);
    REQUIRE(documents2.size() == 1);
    const RopeStorage hostRope2{Rope(text2)};
    const RopeStorage virtualRope2{Rope(documents2[0].documentText)};
    const std::size_t afterOffset = Find(text2, "after");
    REQUIRE(documents2[0].documentText.size() == text2.size());
    REQUIRE(BytePositionToLsp(virtualRope2, afterOffset) == BytePositionToLsp(hostRope2, afterOffset));
}

TEST_CASE("EmbeddedLanguageAtByteOffset resolves point inside/outside owned ranges", "[EmbeddedDocuments]") {
    const Mode        mode = HtmlMode();
    const std::string text = "<div></div><script>let x = 1;</script><style>body{}</style>";

    const auto        documents  = BuildEmbeddedDocuments(mode, text);
    const std::size_t jsOffset   = Find(text, "let x");
    const std::size_t cssOffset  = Find(text, "body{}");
    const std::size_t hostOffset = Find(text, "<div>");

    REQUIRE(EmbeddedLanguageAtByteOffset(documents, jsOffset) == std::optional<std::string>("javascript"));
    REQUIRE(EmbeddedLanguageAtByteOffset(documents, cssOffset) == std::optional<std::string>("css"));
    REQUIRE(EmbeddedLanguageAtByteOffset(documents, hostOffset) == std::nullopt);
}

TEST_CASE("ResolveLspServerKey returns the embedded language at point, or empty when not embedded",
          "[EmbeddedDocuments]") {
    const Mode        mode = HtmlMode();
    const std::string text = "<div></div><script>let x = 1;</script>";

    REQUIRE(ResolveLspServerKey(mode, text, Find(text, "let x")) == "javascript");
    REQUIRE(ResolveLspServerKey(mode, text, Find(text, "<div>")) == "");

    const Mode fundamental = FundamentalMode();
    REQUIRE(ResolveLspServerKey(fundamental, text, 0) == "");
}
