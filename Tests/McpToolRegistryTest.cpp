#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Lsp/LspManager.h"
#include "Editor/Mcp/McpToolRegistry.h"
#include "Editor/ProjectRoot.h"
#include "Editor/TestRun/TestRunner.h"
#include "Editor/Vcs/VcsRunner.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

using ned::editor::ProjectRoot;
using ned::editor::SetProjectRoot;
using ned::editor::lsp::LspManager;
using ned::editor::mcp::ToolRegistry;
using ned::editor::testrun::TestRunner;
using ned::editor::vcs::VcsRunner;
using ned::text::Buffer;
using ned::text::BufferList;

namespace {

// Every ToolRegistry test wires real managers, not fakes -- confirmed via
// Tests/LspManagerTest.cpp's own precedent ("RequestHover resolves
// synchronously to nullopt when the buffer was never synced") that an
// LspManager/VcsRunner with nothing configured/no provider registered
// resolves every request synchronously with an empty/error result, no real
// subprocess or background thread ever involved -- exactly the deterministic
// shape a fast unit test wants.
struct Fixture {
    BufferList   bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager   lspManager{bufferList, eventLoop};
    VcsRunner    vcsRunner{eventLoop};
    TestRunner   testRunner{bufferList, eventLoop};
    ToolRegistry registry{bufferList, lspManager, vcsRunner, testRunner};
};

bool IsError(const ned::editor::mcp::Json& result) {
    return result.value("isError", false);
}

std::string ResultText(const ned::editor::mcp::Json& result) {
    return result.at("content").at(0).at("text").get<std::string>();
}

} // namespace

TEST_CASE("ToolRegistry::ListTools reports every built-in tool", "[Mcp]") {
    Fixture fixture;
    const auto tools = fixture.registry.ListTools();

    const std::vector<std::string> expectedNames = {
        "get_diagnostics", "hover", "goto_definition", "find_references", "git_status", "git_diff", "search_project", "run_tests", "get_test_results",
    };
    REQUIRE(tools.size() == expectedNames.size());
    for (const std::string& name : expectedNames) {
        REQUIRE(fixture.registry.HasTool(name));
        const bool found = std::any_of(tools.begin(), tools.end(), [&](const auto& tool) { return tool.name == name; });
        REQUIRE(found);
    }
}

TEST_CASE("ToolRegistry::CallTool reports an error for an unknown tool name", "[Mcp]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("not_a_real_tool", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE(IsError(result));
    });
    REQUIRE(invoked);
}

TEST_CASE("get_diagnostics reports an error when the file isn't open in ned", "[Mcp]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("get_diagnostics", ned::editor::mcp::Json{{"file", "/definitely/not/open.cpp"}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE(IsError(result));
    });
    REQUIRE(invoked);
}

TEST_CASE("get_diagnostics reports a missing required argument", "[Mcp]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("get_diagnostics", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE(IsError(result));
    });
    REQUIRE(invoked);
}

TEST_CASE("get_diagnostics returns an empty diagnostics array for an open buffer with none", "[Mcp]") {
    Fixture               fixture;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-mcp-registry-test-diag.txt";
    {
        std::ofstream out(path);
        out << "hello world\n";
    }
    fixture.bufferList.OpenOrCreateFile(path);

    bool invoked = false;
    fixture.registry.CallTool("get_diagnostics", ned::editor::mcp::Json{{"file", path.string()}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        const auto parsed = ned::editor::mcp::Json::parse(ResultText(result));
        REQUIRE(parsed.at("diagnostics").empty());
    });
    REQUIRE(invoked);

    std::filesystem::remove(path);
}

TEST_CASE("hover reports an error when required arguments are missing", "[Mcp]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("hover", ned::editor::mcp::Json{{"file", "x"}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE(IsError(result));
    });
    REQUIRE(invoked);
}

TEST_CASE("search_project finds a real match under the project root", "[Mcp]") {
    Fixture                     fixture;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ned-mcp-registry-test-search";
    std::filesystem::create_directories(root);
    {
        std::ofstream out(root / "needle.txt");
        out << "the quick brown fox jumps over the lazy dog\n";
    }
    const std::filesystem::path previousRoot = ProjectRoot();
    SetProjectRoot(root);

    bool invoked = false;
    fixture.registry.CallTool("search_project", ned::editor::mcp::Json{{"pattern", "quick brown fox"}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        const auto parsed = ned::editor::mcp::Json::parse(ResultText(result));
        REQUIRE(parsed.at("totalMatches") == 1);
        REQUIRE(parsed.at("matches").at(0).at("text") == "the quick brown fox jumps over the lazy dog");
    });
    REQUIRE(invoked);

    SetProjectRoot(previousRoot);
    std::filesystem::remove_all(root);
}

TEST_CASE("search_project reports an error for an invalid pattern", "[Mcp]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("search_project", ned::editor::mcp::Json{{"pattern", "("}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE(IsError(result));
    });
    REQUIRE(invoked);
}

TEST_CASE("git_status reports an error when no VCS provider is registered", "[Mcp]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("git_status", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE(IsError(result));
    });
    REQUIRE(invoked);
}

TEST_CASE("get_test_results reports no results before any run_tests call", "[Mcp]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("get_test_results", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result).find("No test results yet") != std::string::npos);
    });
    REQUIRE(invoked);
}

TEST_CASE("run_tests returns immediately with a status message", "[Mcp]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("run_tests", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
    });
    REQUIRE(invoked);
}
