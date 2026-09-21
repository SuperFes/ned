#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

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
