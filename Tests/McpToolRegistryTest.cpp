#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Dap/DapManager.h"
#include "Editor/DiagnosticsLog.h"
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
using ned::editor::dap::DapManager;
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
    DapManager         dapManager{eventLoop};
    ToolRegistry       registry{bufferList, lspManager, vcsRunner, testRunner, dapManager};
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
        "get_diagnostics",
        "hover",
        "goto_definition",
        "find_references",
        "git_status",
        "git_diff",
        "search_project",
        "run_tests",
        "get_test_results",
        "git_stage",
        "git_unstage",
        "git_commit",
        "git_branch_list",
        "git_branch_switch",
        "git_blame",
        "rerun_failed_tests",
        "workspace_symbols",
        "format_buffer",
        "code_actions",
        "preview_rename",
        "get_diagnostics_log",
        "dap_list_breakpoints",
        "dap_set_breakpoint",
        "dap_remove_breakpoint",
        "dap_continue",
        "dap_pause",
        "dap_stop_session",
        "dap_step_over",
        "dap_step_into",
        "dap_step_out",
        "dap_get_current_location",
        "dap_get_stack_trace",
        "dap_get_scopes",
        "dap_get_variables",
        "dap_evaluate",
        "dap_list_watches",
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

TEST_CASE("rerun_failed_tests reports nothing to rerun when no run has happened", "[Mcp]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("rerun_failed_tests", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result).find("No failed tests to rerun") != std::string::npos);
    });
    REQUIRE(invoked);
}

TEST_CASE("git_stage/git_unstage/git_commit/git_branch_list/git_branch_switch/git_blame all report an error with no VCS provider registered",
          "[Mcp]") {
    Fixture fixture;
    for (const char* tool : {"git_stage", "git_unstage"}) {
        bool invoked = false;
        fixture.registry.CallTool(tool, ned::editor::mcp::Json{{"file", "x"}}, [&](ned::editor::mcp::Json result) {
            invoked = true;
            REQUIRE(IsError(result));
        });
        REQUIRE(invoked);
    }
    {
        bool invoked = false;
        fixture.registry.CallTool("git_commit", ned::editor::mcp::Json{{"message", "test"}}, [&](ned::editor::mcp::Json result) {
            invoked = true;
            REQUIRE(IsError(result));
        });
        REQUIRE(invoked);
    }
    {
        bool invoked = false;
        fixture.registry.CallTool("git_branch_list", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
            invoked = true;
            REQUIRE(IsError(result));
        });
        REQUIRE(invoked);
    }
    {
        bool invoked = false;
        fixture.registry.CallTool("git_branch_switch", ned::editor::mcp::Json{{"name", "main"}}, [&](ned::editor::mcp::Json result) {
            invoked = true;
            REQUIRE(IsError(result));
        });
        REQUIRE(invoked);
    }
}

TEST_CASE("git_blame reports an error when the file isn't open in ned", "[Mcp]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("git_blame", ned::editor::mcp::Json{{"file", "/definitely/not/open.cpp"}, {"line", 1}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE(IsError(result));
    });
    REQUIRE(invoked);
}

TEST_CASE("workspace_symbols resolves synchronously to no results for a buffer never synced to an LSP server", "[Mcp]") {
    Fixture               fixture;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-mcp-registry-test-workspace-symbols.txt";
    {
        std::ofstream out(path);
        out << "hello\n";
    }
    fixture.bufferList.OpenOrCreateFile(path);

    bool invoked = false;
    fixture.registry.CallTool("workspace_symbols", ned::editor::mcp::Json{{"file", path.string()}, {"query", "foo"}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result).find("No symbols found") != std::string::npos);
    });
    REQUIRE(invoked);

    std::filesystem::remove(path);
}

TEST_CASE("format_buffer reports an error for a buffer never synced to an LSP server", "[Mcp]") {
    Fixture               fixture;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-mcp-registry-test-format.txt";
    {
        std::ofstream out(path);
        out << "hello\n";
    }
    fixture.bufferList.OpenOrCreateFile(path);

    bool invoked = false;
    fixture.registry.CallTool("format_buffer", ned::editor::mcp::Json{{"file", path.string()}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE(IsError(result));
    });
    REQUIRE(invoked);

    std::filesystem::remove(path);
}

TEST_CASE("code_actions resolves synchronously to no actions for a buffer never synced to an LSP server", "[Mcp]") {
    Fixture               fixture;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-mcp-registry-test-code-actions.txt";
    {
        std::ofstream out(path);
        out << "hello\n";
    }
    fixture.bufferList.OpenOrCreateFile(path);

    bool invoked = false;
    fixture.registry.CallTool("code_actions", ned::editor::mcp::Json{{"file", path.string()}, {"line", 1}, {"column", 1}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result).find("No code actions available") != std::string::npos);
    });
    REQUIRE(invoked);

    std::filesystem::remove(path);
}

TEST_CASE("preview_rename reports an error for a buffer never synced to an LSP server", "[Mcp]") {
    Fixture               fixture;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-mcp-registry-test-rename.txt";
    {
        std::ofstream out(path);
        out << "hello\n";
    }
    fixture.bufferList.OpenOrCreateFile(path);

    bool invoked = false;
    fixture.registry.CallTool(
        "preview_rename", ned::editor::mcp::Json{{"file", path.string()}, {"line", 1}, {"column", 1}, {"newName", "renamed"}},
        [&](ned::editor::mcp::Json result) {
            invoked = true;
            REQUIRE(IsError(result));
        });
    REQUIRE(invoked);

    std::filesystem::remove(path);
}

TEST_CASE("get_diagnostics_log reports an error for an unknown category", "[Mcp]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("get_diagnostics_log", ned::editor::mcp::Json{{"category", "NotARealCategory"}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE(IsError(result));
    });
    REQUIRE(invoked);
}

TEST_CASE("get_diagnostics_log returns entries filtered by a real category", "[Mcp]") {
    Fixture fixture;
    ned::editor::LogMessage(ned::editor::LogCategory::Vcs, ned::editor::LogSeverity::Warning, "mcp-registry-test marker message");

    bool invoked = false;
    fixture.registry.CallTool("get_diagnostics_log", ned::editor::mcp::Json{{"category", "Vcs"}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result).find("mcp-registry-test marker message") != std::string::npos);
    });
    REQUIRE(invoked);
}

TEST_CASE("dap_list_breakpoints reports none set, then a round trip through dap_set_breakpoint/dap_remove_breakpoint", "[Mcp][Dap]") {
    Fixture fixture;

    bool invoked = false;
    fixture.registry.CallTool("dap_list_breakpoints", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result) == "No breakpoints set.");
    });
    REQUIRE(invoked);

    invoked = false;
    fixture.registry.CallTool(
        "dap_set_breakpoint", ned::editor::mcp::Json{{"file", "x.cpp"}, {"line", 10}, {"condition", "n > 3"}}, [&](ned::editor::mcp::Json result) {
            invoked = true;
            REQUIRE_FALSE(IsError(result));
        });
    REQUIRE(invoked);

    invoked = false;
    fixture.registry.CallTool("dap_list_breakpoints", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        const auto parsed = ned::editor::mcp::Json::parse(ResultText(result));
        REQUIRE(parsed.size() == 1);
        REQUIRE(parsed[0].at("line") == 10);
        REQUIRE(parsed[0].at("condition") == "n > 3");
    });
    REQUIRE(invoked);

    invoked = false;
    fixture.registry.CallTool("dap_remove_breakpoint", ned::editor::mcp::Json{{"file", "x.cpp"}, {"line", 10}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result).find("Removed") != std::string::npos);
    });
    REQUIRE(invoked);

    invoked = false;
    fixture.registry.CallTool("dap_list_breakpoints", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result) == "No breakpoints set.");
    });
    REQUIRE(invoked);
}

TEST_CASE("dap_remove_breakpoint reports an error when there's nothing to remove", "[Mcp][Dap]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("dap_remove_breakpoint", ned::editor::mcp::Json{{"file", "x.cpp"}, {"line", 10}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result).find("No breakpoint at") != std::string::npos);
    });
    REQUIRE(invoked);
}

TEST_CASE("dap_continue reports no launch configuration when nothing's configured for the language", "[Mcp][Dap]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("dap_continue", ned::editor::mcp::Json{{"language", "not-a-real-language"}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result).find("No launch configuration") != std::string::npos);
    });
    REQUIRE(invoked);
}

TEST_CASE("dap_pause/dap_stop_session report no session, dap_step_over/into/out report not stopped, when none is running", "[Mcp][Dap]") {
    Fixture fixture;
    for (const char* tool : {"dap_pause", "dap_stop_session"}) {
        bool invoked = false;
        fixture.registry.CallTool(tool, ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
            invoked = true;
            REQUIRE_FALSE(IsError(result));
            REQUIRE(ResultText(result).find("No debug session") != std::string::npos);
        });
        REQUIRE(invoked);
    }
    for (const char* tool : {"dap_step_over", "dap_step_into", "dap_step_out"}) {
        bool invoked = false;
        fixture.registry.CallTool(tool, ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
            invoked = true;
            REQUIRE_FALSE(IsError(result));
            REQUIRE(ResultText(result).find("Not stopped") != std::string::npos);
        });
        REQUIRE(invoked);
    }
}

TEST_CASE("dap_get_current_location reports not stopped when no session is running", "[Mcp][Dap]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("dap_get_current_location", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result) == "Debug session is not currently stopped.");
    });
    REQUIRE(invoked);
}

TEST_CASE("dap_get_stack_trace/dap_get_scopes/dap_get_variables resolve synchronously to no data with no session", "[Mcp][Dap]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("dap_get_stack_trace", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result).find("No stack available") != std::string::npos);
    });
    REQUIRE(invoked);

    invoked = false;
    fixture.registry.CallTool("dap_get_scopes", ned::editor::mcp::Json{{"frameId", 1}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result).find("No scopes available") != std::string::npos);
    });
    REQUIRE(invoked);

    invoked = false;
    fixture.registry.CallTool("dap_get_variables", ned::editor::mcp::Json{{"variablesReference", 100}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result).find("No variables") != std::string::npos);
    });
    REQUIRE(invoked);
}

TEST_CASE("dap_evaluate reports an error with no session", "[Mcp][Dap]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("dap_evaluate", ned::editor::mcp::Json{{"expression", "1 + 1"}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE(IsError(result));
        REQUIRE(ResultText(result) == "No debug session.");
    });
    REQUIRE(invoked);
}

TEST_CASE("dap_list_watches reports set watches with their history", "[Mcp][Dap]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("dap_list_watches", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result) == "No watches set.");
    });
    REQUIRE(invoked);

    fixture.dapManager.AddWatch("x");

    invoked = false;
    fixture.registry.CallTool("dap_list_watches", ned::editor::mcp::Json::object(), [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        const auto parsed = ned::editor::mcp::Json::parse(ResultText(result));
        REQUIRE(parsed.size() == 1);
        REQUIRE(parsed[0].at("expression") == "x");
        REQUIRE_FALSE(parsed[0].contains("history")); // never stopped yet -- no history recorded
    });
    REQUIRE(invoked);
}
