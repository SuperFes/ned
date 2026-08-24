#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/OrgCapture.h"
#include "Text/Buffer.h"
#include "Text/Rope.h"

using ned::editor::org::CaptureExpansion;
using ned::editor::org::CaptureResult;
using ned::editor::org::CaptureTemplate;
using ned::editor::org::CaptureTemplateForKey;
using ned::editor::org::CaptureTemplates;
using ned::editor::org::ExpandCaptureTemplate;
using ned::editor::org::InsertCapture;
using ned::editor::org::RegisterCaptureTemplate;
using ned::text::Buffer;
using ned::text::Rope;

TEST_CASE("ExpandCaptureTemplate strips the first %? and reports its offset", "[OrgCapture]") {
    const CaptureExpansion expansion = ExpandCaptureTemplate("* TODO %?\n");
    REQUIRE(expansion.text == "* TODO \n");
    REQUIRE(expansion.cursorOffset == 7);
}

TEST_CASE("ExpandCaptureTemplate with no %? has no cursor offset", "[OrgCapture]") {
    const CaptureExpansion expansion = ExpandCaptureTemplate("* Fixed note\n");
    REQUIRE(expansion.text == "* Fixed note\n");
    REQUIRE_FALSE(expansion.cursorOffset.has_value());
}

TEST_CASE("ExpandCaptureTemplate at the very start", "[OrgCapture]") {
    const CaptureExpansion expansion = ExpandCaptureTemplate("%?* TODO\n");
    REQUIRE(expansion.text == "* TODO\n");
    REQUIRE(expansion.cursorOffset == 0);
}

TEST_CASE("ExpandCaptureTemplate only treats the first %? specially", "[OrgCapture]") {
    const CaptureExpansion expansion = ExpandCaptureTemplate("%? and %?\n");
    REQUIRE(expansion.text == " and %?\n");
    REQUIRE(expansion.cursorOffset == 0);
}

TEST_CASE("InsertCapture with no headline configured appends at end of buffer", "[OrgCapture]") {
    Buffer                buffer("test", Rope("* Existing\nBody text\n"));
    const CaptureTemplate tmpl{'t', "Todo", "test", "* TODO %?\n", ""};
    const CaptureResult   result = InsertCapture(buffer, tmpl);
    REQUIRE(buffer.Text() == "* Existing\nBody text\n* TODO \n");
    REQUIRE(result.headlineFound);
    REQUIRE(result.insertedAt == std::string("* Existing\nBody text\n* TODO ").size());
}

TEST_CASE("InsertCapture appends into an empty buffer with no leading blank line", "[OrgCapture]") {
    Buffer                buffer("test", Rope(""));
    const CaptureTemplate tmpl{'t', "Todo", "test", "* TODO %?\n", ""};
    const CaptureResult   result = InsertCapture(buffer, tmpl);
    REQUIRE(buffer.Text() == "* TODO \n");
    REQUIRE(result.insertedAt == std::string("* TODO ").size());
}

TEST_CASE("InsertCapture files under a matching headline as its subtree's last child", "[OrgCapture]") {
    Buffer                buffer("test", Rope("* Inbox\n** existing child\n* Other\nOther body\n"));
    const CaptureTemplate tmpl{'t', "Todo", "test", "** TODO %?\n", "Inbox"};
    const CaptureResult   result = InsertCapture(buffer, tmpl);
    REQUIRE(buffer.Text() == "* Inbox\n** existing child\n** TODO \n* Other\nOther body\n");
    REQUIRE(result.headlineFound);
}

TEST_CASE("InsertCapture falls back to end of buffer when the headline isn't found", "[OrgCapture]") {
    Buffer                buffer("test", Rope("* Something else\n"));
    const CaptureTemplate tmpl{'t', "Todo", "test", "* TODO %?\n", "Inbox"};
    const CaptureResult   result = InsertCapture(buffer, tmpl);
    REQUIRE(buffer.Text() == "* Something else\n* TODO \n");
    REQUIRE_FALSE(result.headlineFound);
}

TEST_CASE("InsertCapture with no %? in the template lands point at the end of the inserted text",
          "[OrgCapture]") {
    Buffer                buffer("test", Rope(""));
    const CaptureTemplate tmpl{'n', "Note", "test", "* Fixed note\n", ""};
    const CaptureResult   result = InsertCapture(buffer, tmpl);
    REQUIRE(buffer.Text() == "* Fixed note\n");
    REQUIRE(result.insertedAt == buffer.Text().size());
}

TEST_CASE("RegisterCaptureTemplate/CaptureTemplateForKey round-trip and overwrite by key", "[OrgCapture]") {
    RegisterCaptureTemplate(CaptureTemplate{'z', "First", "a.org", "* A %?\n", ""});
    REQUIRE(CaptureTemplateForKey('z')->name == "First");

    RegisterCaptureTemplate(CaptureTemplate{'z', "Second", "b.org", "* B %?\n", ""});
    REQUIRE(CaptureTemplateForKey('z')->name == "Second");
    REQUIRE(CaptureTemplateForKey('z')->targetFile == "b.org");
}

TEST_CASE("CaptureTemplateForKey returns nullopt for an unregistered key", "[OrgCapture]") {
    REQUIRE_FALSE(CaptureTemplateForKey('\x01').has_value());
}

TEST_CASE("CaptureTemplates lists every registered template in key order", "[OrgCapture]") {
    RegisterCaptureTemplate(CaptureTemplate{'y', "Y", "y.org", "%?\n", ""});
    RegisterCaptureTemplate(CaptureTemplate{'x', "X", "x.org", "%?\n", ""});
    const auto templates = CaptureTemplates();

    auto indexOf = [&](char key) {
        for (std::size_t i = 0; i < templates.size(); ++i) {
            if (templates[i].key == key)
                return i;
        }
        return templates.size();
    };
    REQUIRE(indexOf('x') < indexOf('y'));
}
