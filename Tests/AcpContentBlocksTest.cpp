#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Acp/ContentBlocks.h"

using namespace ned::editor::acp;

TEST_CASE("Base64DecodedSize counts decoded bytes, ignoring padding", "[Acp][AcpContent]") {
    REQUIRE(Base64DecodedSize("") == 0);
    REQUIRE(Base64DecodedSize("QQ==") == 1);
    REQUIRE(Base64DecodedSize("QUI=") == 2);
    REQUIRE(Base64DecodedSize("QUJD") == 3);
    REQUIRE(Base64DecodedSize("QUJD\nQUJD") == 6);
}

TEST_CASE("FormatByteSize picks B, KB or MB", "[Acp][AcpContent]") {
    REQUIRE(FormatByteSize(512) == "512 B");
    REQUIRE(FormatByteSize(84 * 1024) == "84 KB");
    REQUIRE(FormatByteSize(3 * 1024 * 1024 + 512 * 1024) == "3.5 MB");
}

TEST_CASE("FileUriPath decodes file uris and rejects others", "[Acp][AcpContent]") {
    REQUIRE(FileUriPath("file:///src/a%20b.cpp") == "/src/a b.cpp");
    REQUIRE(FileUriPath("file:///50%") == "/50%");
    REQUIRE_FALSE(FileUriPath("https://example.com/x"));
}

TEST_CASE("AgentContentEntry names each block type", "[Acp][AcpContent]") {
    REQUIRE_FALSE(AgentContentEntry(Json{{"type", "text"}, {"text", "hi"}}));
    REQUIRE_FALSE(AgentContentEntry(Json{{"type", "image"}}));
    REQUIRE_FALSE(AgentContentEntry(Json{{"type", "resource_link"}}));

    const auto remote = AgentContentEntry(Json{{"type", "image"}, {"mimeType", "image/jpeg"}, {"uri", "https://x.test/cat.jpg?s=1"}});
    REQUIRE(remote);
    REQUIRE(remote->contentName == "cat.jpg");
    REQUIRE(remote->detail == "https://x.test/cat.jpg?s=1");

    const auto titled = AgentContentEntry(Json{{"type", "resource_link"}, {"uri", "file:///a/b.txt"}, {"name", "b.txt"}, {"title", "The B file"}, {"size", 2048}});
    REQUIRE(titled->contentName == "The B file");
    REQUIRE(titled->byteSize == 2048);

    const auto blob = AgentContentEntry(Json{{"type", "resource"}, {"resource", {{"uri", "file:///a/b.bin"}, {"blob", "QUJD"}, {"mimeType", "application/octet-stream"}}}});
    REQUIRE(blob->text.empty());
    REQUIRE(blob->byteSize == 3);
}

TEST_CASE("AppendAttachmentName grows one trailing marker", "[Acp][AcpContent]") {
    std::string text = "look";
    AppendAttachmentName(text, "a.png");
    AppendAttachmentName(text, "b.cpp");
    REQUIRE(text == "look\n\n[attached: a.png, b.cpp]");

    std::string quoted = "see [attached: x]\nand more";
    AppendAttachmentName(quoted, "y");
    REQUIRE(quoted == "see [attached: x]\nand more\n\n[attached: y]");

    REQUIRE(AttachmentName(Json{{"type", "resource_link"}, {"uri", "file:///x"}}).empty());
    REQUIRE(AttachmentName(Json{{"type", "audio"}, {"data", "QQ=="}}) == "audio");
}

TEST_CASE("ContentText joins the text of a block or an array", "[Acp][AcpContent]") {
    REQUIRE(ContentText(Json{{"type", "text"}, {"text", "a"}}) == "a");
    REQUIRE(ContentText(Json::array({Json{{"type", "text"}, {"text", "a"}}, Json{{"type", "image"}}, Json{{"type", "text"}, {"text", "b"}}})) == "ab");
    REQUIRE(ContentText(Json()).empty());
}
