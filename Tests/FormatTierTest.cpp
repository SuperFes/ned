#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Format.h"
#include "Editor/FormatRules.h"
#include "Editor/Lsp/ServerConfig.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::ApplyNativeFormat;
using ned::editor::BracePlacement;
using ned::editor::Mode;
using ned::editor::PhpMode;
using ned::editor::SetBracePlacement;
using ned::editor::lsp::FormatBufferEnabled;
using ned::editor::lsp::SetLspFormatBufferEnabled;
using ned::text::Buffer;

namespace {

// Both stores are process-wide.
struct TierGuard {
    ~TierGuard() {
        SetBracePlacement("brace.control", std::nullopt);
        SetLspFormatBufferEnabled("", std::nullopt);
        SetLspFormatBufferEnabled("php", std::nullopt);
    }
};

} // namespace

TEST_CASE("The format-buffer LSP tier defaults on and is settable per language", "[Format][Lsp]") {
    const TierGuard guard;

    CHECK(FormatBufferEnabled("php"));
    CHECK(FormatBufferEnabled("cpp"));

    // A language with no entry of its own reads the process-wide default.
    SetLspFormatBufferEnabled("", false);
    CHECK_FALSE(FormatBufferEnabled("php"));
    CHECK_FALSE(FormatBufferEnabled("cpp"));

    // A per-language entry wins over that default, in both directions.
    SetLspFormatBufferEnabled("php", true);
    CHECK(FormatBufferEnabled("php"));
    CHECK_FALSE(FormatBufferEnabled("cpp"));

    // nil clears the language back to whatever the default currently is.
    SetLspFormatBufferEnabled("php", std::nullopt);
    CHECK_FALSE(FormatBufferEnabled("php"));

    SetLspFormatBufferEnabled("", std::nullopt);
    SetLspFormatBufferEnabled("php", false);
    CHECK_FALSE(FormatBufferEnabled("php"));
    CHECK(FormatBufferEnabled("cpp"));
}

// The tier setting exists so a configured rule can actually reach a buffer
// whose language also has a server running -- which means the Native tier
// has to apply that rule when it runs. Asserted on the applied result, not
// on an edit list.
TEST_CASE("ApplyNativeFormat applies a configured brace rule to a PHP buffer", "[Format]") {
    const TierGuard guard;
    SetBracePlacement("brace.control", BracePlacement::NextLine);

    const Mode mode = PhpMode();
    Buffer     buffer("t.php");
    buffer.InsertAtPoint("<?php\nfunction f($x) {\n    if ($x) {\n        echo \"a\";\n    }\n}\n");

    REQUIRE(ApplyNativeFormat(buffer, &mode));
    CHECK(buffer.Text() == "<?php\nfunction f($x) {\n    if ($x)\n    {\n        echo \"a\";\n    }\n}\n");
}

// A pass whose edits would change how the code parses is dropped whole. Here
// the comment captures that normally stop a brace being pulled onto `// note`
// are stripped, so only the structure check is left to catch it.
TEST_CASE("ApplyNativeFormat drops a pass that would change how the code parses", "[Format]") {
    const TierGuard guard;
    SetBracePlacement("brace.control", BracePlacement::SameLine);

    Mode mode           = ned::editor::CppMode();
    mode.formatCaptures = [inner = mode.formatCaptures](std::string_view text) {
        std::vector<ned::editor::FormatCapture> captures = inner(text);
        std::erase_if(captures, [](const ned::editor::FormatCapture& capture) { return capture.name == "comment"; });
        return captures;
    };
    const std::string source = "void f() {\n    if (x) // note\n    {\n        g();\n    }\n}\n";
    Buffer            buffer("t.cpp");
    buffer.InsertAtPoint(source);

    ApplyNativeFormat(buffer, &mode);
    CHECK(buffer.Text() == source);

    mode.sameStructure = nullptr; // the unchecked pipeline really would have joined it
    ApplyNativeFormat(buffer, &mode);
    CHECK(buffer.Text().find("// note {") != std::string::npos);
}

TEST_CASE("FormatEditsKeepStructure tells whitespace from meaning", "[Format]") {
    const Mode        mode = ned::editor::CppMode();
    const std::string text = "int a; // x\nint b;\n";
    const std::size_t gap  = text.find('\n');
    CHECK(ned::editor::FormatEditsKeepStructure(mode, text, {{gap + 1, gap + 1, "    "}}));
    CHECK_FALSE(ned::editor::FormatEditsKeepStructure(mode, text, {{gap, gap + 1, " "}})); // comments out `int b;`
    CHECK(ned::editor::ApplyFormatTextEditsToText(text, {{gap, gap + 1, " "}, {0, 3, "long"}}) == "long a; // x int b;\n");
}

// A buffer with no major mode still gets the Hygiene pass rather than
// crashing on a null Mode -- the shape BufferView's LSP fallback can hit.
TEST_CASE("ApplyNativeFormat tolerates a null mode", "[Format]") {
    Buffer buffer("t.txt");
    buffer.InsertAtPoint("a   \nb");

    REQUIRE(ApplyNativeFormat(buffer, nullptr));
    CHECK(buffer.Text() == "a\nb\n");
}

// The key a user writes in init.janet must be the key the format-buffer gate
// reads. Both come from LanguageKeyForMode, but the setting is typed by hand
// against a different list (ned/set-lsp-command's own), so this holds the two
// together for the languages whose style rules exist today.
TEST_CASE("The tier key matches LanguageKeyForMode", "[Format][Lsp]") {
    CHECK(ned::editor::LanguageKeyForMode(ned::editor::PhpMode()) == "php");
    CHECK(ned::editor::LanguageKeyForMode(ned::editor::CppMode()) == "cpp");
    CHECK(ned::editor::LanguageKeyForMode(ned::editor::PythonMode()) == "python");
}
