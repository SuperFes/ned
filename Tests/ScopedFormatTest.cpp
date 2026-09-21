#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>

#include "Editor/FinalNewline.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Editor/ScopedFormat.h"
#include "Editor/TrimOnSave.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::ApplyScopedFormatOnSave;
using ned::editor::CppMode;
using ned::editor::EnsureFinalNewline;
using ned::editor::FundamentalMode;
using ned::editor::Mode;
using ned::editor::SetBreakBefore;
using ned::editor::SetEnsureFinalNewline;
using ned::editor::SetSpaceBefore;
using ned::editor::SetTrimTrailingWhitespaceOnSave;
using ned::editor::TrimTrailingWhitespaceOnSave;

namespace {

// automatic-scoped-on-save follow-up: same test-isolation lesson every
// other FormatRulesGuard in this rollout already learned once.
struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetSpaceBefore("control.parens", std::nullopt);
        SetSpaceBefore("cpp/control.parens", std::nullopt);
        SetBreakBefore("control.keyword", std::nullopt);
    }
};

struct TrimGuard {
    bool previous = TrimTrailingWhitespaceOnSave();
    ~TrimGuard() {
        SetTrimTrailingWhitespaceOnSave(previous);
    }
};

struct FinalNewlineGuard {
    bool previous = EnsureFinalNewline();
    ~FinalNewlineGuard() {
        SetEnsureFinalNewline(previous);
    }
};

} // namespace

TEST_CASE("ApplyScopedFormatOnSave reindents only the region touched since the last save", "[ScopedFormat]") {
    const Mode mode = CppMode();
    ned::text::Buffer buffer("scratch", ned::text::Rope("void a() {\nint x = 1;\n}\n\nvoid b() {\nint y = 2;\n}\n"));
    // Constructing already snapshots this as "saved" -- nothing is touched
    // yet, so this edit is the only thing UnsavedChangeRanges() will name.
    const std::size_t bLineOffset = buffer.Text().find("int y = 2;");
    buffer.SetPoint(bLineOffset + 4); // the "y" itself
    buffer.DeleteRange(bLineOffset + 4, 1);
    buffer.InsertAtPoint("Y");

    REQUIRE(ApplyScopedFormatOnSave(buffer, mode));

    const std::string result = buffer.Text();
    REQUIRE(result.find("\nint x = 1;\n") != std::string::npos);   // untouched function: still misindented
    REQUIRE(result.find("\n    int Y = 2;\n") != std::string::npos); // touched function: reindented
}

TEST_CASE("ApplyScopedFormatOnSave scopes the Space rule pass the same way", "[ScopedFormat]") {
    const FormatRulesGuard guard;
    SetSpaceBefore("control.parens", true);

    const Mode mode = CppMode();
    ned::text::Buffer buffer("scratch", ned::text::Rope("void a() {\n    if(x) {\n    }\n}\n\nvoid b() {\n    if(y) {\n    }\n}\n"));

    const std::size_t bIfOffset = buffer.Text().rfind("if(y)");
    // A real content edit is what registers an unsaved-change range --
    // rewriting the "if(y)" line in place with identical text marks it
    // touched without changing what it says.
    const std::size_t lineStart = buffer.Text().rfind('\n', bIfOffset) + 1;
    const std::size_t lineEnd   = buffer.Text().find('\n', bIfOffset);
    const std::string lineText  = buffer.Text().substr(lineStart, lineEnd - lineStart);
    buffer.DeleteRange(lineStart, lineEnd - lineStart);
    buffer.InsertAt(lineStart, lineText);

    REQUIRE(ApplyScopedFormatOnSave(buffer, mode));

    const std::string result = buffer.Text();
    REQUIRE(result.find("if(x)") != std::string::npos);  // untouched -- unchanged
    REQUIRE(result.find("if (y)") != std::string::npos); // touched -- space inserted
}

TEST_CASE("ApplyScopedFormatOnSave runs the keyword-break pass, scoped the same way", "[ScopedFormat]") {
    // The pass this path was missing entirely: format-buffer and `ned
    // --format` applied control.keyword, save did not. Both now walk one
    // list (Editor/FormatPasses.h), so a new kind cannot reach two of the
    // three entry points and quietly skip the third.
    const FormatRulesGuard guard;
    SetBreakBefore("control.keyword", true);

    const Mode        mode = CppMode();
    ned::text::Buffer buffer(
        "scratch", ned::text::Rope("void a() {\n    if (x) {\n    } else {\n    }\n}\n\nvoid b() {\n    if (y) {\n    } else {\n    }\n}\n"));

    const std::size_t bElse     = buffer.Text().rfind("} else {");
    const std::size_t lineStart = buffer.Text().rfind('\n', bElse) + 1;
    const std::size_t lineEnd   = buffer.Text().find('\n', bElse);
    const std::string lineText  = buffer.Text().substr(lineStart, lineEnd - lineStart);
    buffer.DeleteRange(lineStart, lineEnd - lineStart);
    buffer.InsertAt(lineStart, lineText);

    REQUIRE(ApplyScopedFormatOnSave(buffer, mode));

    const std::string result = buffer.Text();
    const std::size_t split  = result.find("void b()");
    REQUIRE(result.substr(0, split).find("} else {") != std::string::npos);   // untouched -- unchanged
    REQUIRE(result.substr(split).find("}\n    else {") != std::string::npos); // touched -- broken
}

TEST_CASE("ApplyScopedFormatOnSave trims trailing whitespace only on touched lines", "[ScopedFormat]") {
    const TrimGuard guard;
    SetTrimTrailingWhitespaceOnSave(true);

    const Mode mode = FundamentalMode();
    ned::text::Buffer buffer("scratch", ned::text::Rope("keep   \ntouch   \n"));

    const std::size_t touchOffset = buffer.Text().find("touch");
    buffer.SetPoint(touchOffset);
    buffer.DeleteRange(touchOffset, 5);
    buffer.InsertAtPoint("edit1");

    REQUIRE(ApplyScopedFormatOnSave(buffer, mode));

    const std::string result = buffer.Text();
    REQUIRE(result.find("keep   \n") != std::string::npos); // untouched line's trailing spaces survive
    REQUIRE(result.find("edit1\n") != std::string::npos);   // touched line's trailing spaces stripped
}

TEST_CASE("ApplyScopedFormatOnSave does not trim when set-trim-trailing-whitespace-on-save is disabled",
          "[ScopedFormat]") {
    const TrimGuard guard;
    SetTrimTrailingWhitespaceOnSave(false);

    const Mode mode = FundamentalMode();
    ned::text::Buffer buffer("scratch", ned::text::Rope("touch   \n"));
    buffer.SetPoint(0);
    buffer.DeleteRange(0, 5);
    buffer.InsertAt(0, "edit1");

    ApplyScopedFormatOnSave(buffer, mode);
    REQUIRE(buffer.Text() == "edit1   \n");
}

TEST_CASE("ApplyScopedFormatOnSave appends a final newline only when the touched region reaches the buffer's "
          "true end",
          "[ScopedFormat]") {
    const FinalNewlineGuard guard;
    SetEnsureFinalNewline(true);

    const Mode mode = FundamentalMode();

    SECTION("touched region is the last line -- newline appended") {
        ned::text::Buffer buffer("scratch", ned::text::Rope("first\nlast"));
        buffer.SetPoint(buffer.Text().find("last"));
        buffer.DeleteRange(buffer.Text().find("last"), 4);
        buffer.InsertAtPoint("LAST");

        REQUIRE(ApplyScopedFormatOnSave(buffer, mode));
        REQUIRE(buffer.Text() == "first\nLAST\n");
    }

    SECTION("touched region does NOT reach the last line -- no newline added") {
        ned::text::Buffer buffer("scratch", ned::text::Rope("first\nlast"));
        buffer.SetPoint(0);
        buffer.DeleteRange(0, 5);
        buffer.InsertAt(0, "FIRST");

        ApplyScopedFormatOnSave(buffer, mode);
        REQUIRE(buffer.Text() == "FIRST\nlast"); // still missing its final newline -- untouched region
    }
}

TEST_CASE("ApplyScopedFormatOnSave is a no-op with no unsaved changes at all", "[ScopedFormat]") {
    const Mode         mode = CppMode();
    ned::text::Buffer  buffer("scratch", ned::text::Rope("void f(){\nint x=1;\n}\n"));
    REQUIRE(buffer.UnsavedChangeRanges().empty());
    REQUIRE_FALSE(ApplyScopedFormatOnSave(buffer, mode));
    REQUIRE(buffer.Text() == "void f(){\nint x=1;\n}\n");
}

TEST_CASE("ApplyScopedFormatOnSave is a no-op for a huge buffer", "[ScopedFormat]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_scoped_format_test_huge.cpp";
    { std::ofstream(path) << "void f(){\nint x=1;\n}\n"; }

    ned::text::SetHugeFileThreshold(4); // well under this file's real size
    ned::text::Buffer buffer = ned::text::Buffer::FromHugeFile(path);
    REQUIRE(buffer.Content().IsHuge());

    buffer.SetPoint(0);
    buffer.DeleteRange(0, 4);
    buffer.InsertAt(0, "VOID");

    REQUIRE_FALSE(ApplyScopedFormatOnSave(buffer, CppMode()));

    ned::text::SetHugeFileThreshold(1024ull * 1024 * 1024);
    std::filesystem::remove(path);
}
