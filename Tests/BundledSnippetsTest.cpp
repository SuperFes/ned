#include <catch2/catch_test_macros.hpp>

#include "Editor/BundledSnippets.h"
#include "Editor/Snippet.h"
#include "Editor/SnippetRegistry.h"

using ned::editor::ClearAllSnippets;
using ned::editor::ParseSnippet;
using ned::editor::RegisterBundledSnippets;
using ned::editor::SnippetBodyForTrigger;
using ned::editor::SnippetTriggers;
using ned::editor::SnippetVariables;

namespace {

// The registry is process-wide static state -- SnippetRegistryTest.cpp's
// own guard shape.
struct SnippetRegistryGuard {
    SnippetRegistryGuard() {
        ClearAllSnippets();
    }
    ~SnippetRegistryGuard() {
        ClearAllSnippets();
    }
};

} // namespace

TEST_CASE("RegisterBundledSnippets registers entries for the expected languages", "[BundledSnippets]") {
    const SnippetRegistryGuard guard;
    RegisterBundledSnippets();

    for (const char* language : {"c", "cpp", "python", "javascript", "typescript", "tsx", "html", "css", "bash",
                                 "markdown", "janet"}) {
        INFO("language: " << language);
        REQUIRE_FALSE(SnippetTriggers(language).empty());
    }
    // Data-format/config and niche languages deliberately have no bundled
    // entries (BundledSnippets.cpp's own doc comment) -- SnippetTriggers("")
    // still applies since it's the merged "language tier + global tier"
    // query and nothing was ever registered under the "" global tier here.
    REQUIRE(SnippetTriggers("json").empty());
    REQUIRE(SnippetTriggers("yaml").empty());
}

// Every bundled body must be well-formed snippet syntax -- if any of them
// hit ParseSnippet's "ill-formed" fallback (a typo'd $/{/} somewhere), the
// resulting text would still contain a literal '$' character (none of the
// bundled bodies want one in their actual output), so this is a real,
// automatic correctness check across the whole table without hand-writing
// per-entry assertions.
TEST_CASE("Every bundled snippet body parses cleanly with no leftover '$' in its text", "[BundledSnippets]") {
    const SnippetRegistryGuard guard;
    RegisterBundledSnippets();

    for (const char* language : {"c", "cpp", "python", "javascript", "typescript", "tsx", "html", "css", "bash",
                                 "markdown", "janet"}) {
        for (const std::string& trigger : SnippetTriggers(language)) {
            INFO("language: " << language << ", trigger: " << trigger);
            const auto body = SnippetBodyForTrigger(language, trigger);
            REQUIRE(body.has_value());
            const auto parsed = ParseSnippet(*body);
            REQUIRE(parsed.text.find('$') == std::string::npos);
            REQUIRE_FALSE(parsed.fields.empty()); // the implicit final stop, at minimum
        }
    }
}

TEST_CASE("The bundled C/C++ include-guard snippet resolves TM_FILENAME_BASE through an /upcase transform",
          "[BundledSnippets]") {
    const SnippetRegistryGuard guard;
    RegisterBundledSnippets();

    const auto body = SnippetBodyForTrigger("cpp", "guard");
    REQUIRE(body.has_value());

    SnippetVariables vars;
    vars.filenameBase = "MyHeader";
    const auto parsed = ParseSnippet(*body, vars);
    REQUIRE(parsed.text.find("MYHEADER_H") != std::string::npos);
    REQUIRE(parsed.text.find("#ifndef MYHEADER_H") == 0);

    // No SnippetVariables supplied (e.g. an unsaved buffer with no path
    // yet): the variable resolves to "" rather than leaving the raw
    // ${TM_FILENAME_BASE/.../} construct visible -- degenerate, not broken.
    const auto degenerate = ParseSnippet(*body);
    REQUIRE(degenerate.text.find('$') == std::string::npos);
}
