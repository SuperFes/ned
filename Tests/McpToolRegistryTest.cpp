#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <stdlib.h>

#include "Editor/Backup.h"
#include "Editor/Dap/Manager.h"
#include "Editor/DiagnosticsLog.h"
#include "Editor/Lsp/EditApply.h"
#include "Editor/Lsp/Manager.h"
#include "Editor/Mcp/ToolRegistry.h"
#include "Editor/OrgCapture.h"
#include "Editor/Project/Root.h"
#include "Editor/TestRun/TestRunner.h"
#include "Editor/Vcs/Runner.h"
#include "FakeLspServer.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

using ned::editor::ProjectRoot;
using ned::editor::SetProjectRoot;
using ned::editor::mcp::ToolRegistry;
using ned::editor::testrun::TestRunner;
using ned::editor::vcs::Runner;
using ned::text::Buffer;
using ned::text::BufferList;

namespace {

// Every ToolRegistry test wires real managers, not fakes -- confirmed via
// Tests/ManagerTest.cpp's own precedent ("RequestHover resolves
// synchronously to nullopt when the buffer was never synced") that an
// Manager/Runner with nothing configured/no provider registered
// resolves every request synchronously with an empty/error result, no real
// subprocess or background thread ever involved -- exactly the deterministic
// shape a fast unit test wants.
struct Fixture {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    ned::editor::lsp::Manager lspManager{bufferList, eventLoop};
    Runner          vcsRunner{eventLoop};
    TestRunner         testRunner{bufferList, eventLoop};
    ned::editor::dap::Manager dapManager{eventLoop};
    ToolRegistry       registry{bufferList, lspManager, vcsRunner, testRunner, dapManager};
};

bool IsError(const ned::editor::mcp::Json& result) {
    return result.value("isError", false);
}

std::string ResultText(const ned::editor::mcp::Json& result) {
    return result.at("content").at(0).at("text").get<std::string>();
}

using ned::editor::mcp::Json;
using ResolvedRename = ned::editor::lsp::Manager::ResolvedRename;

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        std::string pattern = (std::filesystem::temp_directory_path() / "ned-mcp-apply-XXXXXX").string();
        REQUIRE(::mkdtemp(pattern.data()) != nullptr);
        path = pattern;
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

// Saves made by the tools under test must not leave backup versions in the
// real state directory.
struct BackupsOff {
    bool previous = ned::editor::BackupVersionsEnabled();
    BackupsOff() {
        ned::editor::SetBackupVersionsEnabled(false);
    }
    ~BackupsOff() {
        ned::editor::SetBackupVersionsEnabled(previous);
    }
};

std::filesystem::path WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
    return path;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Stands in for the pane: opens each touched file and applies its edits.
ned::editor::mcp::EditorHooks ApplyingHooks(BufferList& bufferList, std::vector<std::string>& labels) {
    return {.applyWorkspaceEdit = [&bufferList, &labels](const ResolvedRename& edit, const std::string& label) {
        labels.push_back(label);
        for (const auto& fileEdit : edit.edits) {
            ned::editor::lsp::ApplyWorkspaceTextEdits(bufferList.OpenOrCreateFile(fileEdit.path), fileEdit.edits);
        }
        return true;
    }};
}

Json ReplaceEdit(int line, int startCharacter, int endCharacter, const std::string& newText) {
    return Json{{"range", {{"start", {{"line", line}, {"character", startCharacter}}}, {"end", {{"line", line}, {"character", endCharacter}}}}},
                {"newText", newText}};
}

Json Response(const Json& request, Json result) {
    return Json{{"jsonrpc", "2.0"}, {"id", request.at("id")}, {"result", std::move(result)}};
}

} // namespace

TEST_CASE("ToolRegistry::ListTools reports every built-in tool", "[Mcp]") {
    Fixture    fixture;
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
        "rename_symbol",
        "apply_code_action",
        "goto_location",
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
        "capture_note",
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
    Fixture                     fixture;
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
    Fixture                     fixture;
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
    Fixture                     fixture;
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
    Fixture                     fixture;
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
    Fixture                     fixture;
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

// mcp-capture-note follow-up (ROADMAP "Org capture_note").
TEST_CASE("capture_note inserts the given text at a registered template's %? and creates the target file", "[Mcp]") {
    Fixture                     fixture;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-mcp-registry-test-capture.org";
    std::filesystem::remove(path);
    ned::editor::org::RegisterCaptureTemplate(
        ned::editor::org::CaptureTemplate{'m', "MCP note", path.string(), "* TODO %?\n", ""});

    bool invoked = false;
    fixture.registry.CallTool("capture_note", ned::editor::mcp::Json{{"key", "m"}, {"text", "buy milk"}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE_FALSE(IsError(result));
        REQUIRE(ResultText(result).find("MCP note") != std::string::npos);
    });
    REQUIRE(invoked);

    Buffer* target = fixture.bufferList.FindByPath(path);
    REQUIRE(target != nullptr);
    REQUIRE(target->Text() == "* TODO buy milk\n");

    std::filesystem::remove(path);
}

TEST_CASE("capture_note reports an error for an unregistered template key", "[Mcp]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("capture_note", ned::editor::mcp::Json{{"key", "\x01"}, {"text", "x"}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE(IsError(result));
        REQUIRE(ResultText(result).find("No capture template registered") != std::string::npos);
    });
    REQUIRE(invoked);
}

TEST_CASE("capture_note reports a missing required argument", "[Mcp]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.CallTool("capture_note", ned::editor::mcp::Json{{"key", "m"}}, [&](ned::editor::mcp::Json result) {
        invoked = true;
        REQUIRE(IsError(result));
    });
    REQUIRE(invoked);
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

TEST_CASE("rename_symbol and apply_code_action report unavailable before the editor hooks are set", "[Mcp]") {
    Fixture fixture;
    for (const char* tool : {"rename_symbol", "apply_code_action"}) {
        bool invoked = false;
        fixture.registry.CallTool(tool, Json{{"file", "a.txt"}, {"line", 1}, {"column", 1}, {"newName", "x"}, {"title", "x"}},
                                  [&](Json result) {
                                      invoked = true;
                                      REQUIRE(IsError(result));
                                      REQUIRE(ResultText(result).find("isn't available") != std::string::npos);
                                  });
        REQUIRE(invoked);
    }
}

TEST_CASE("rename_symbol applies the server's edit, saving only files that had no unsaved changes", "[Mcp]") {
    Fixture    fixture;
    TempDir    dir;
    BackupsOff backupsOff;
    const auto ownPath   = WriteFile(dir.path / "own.txt", "old_name here\n");
    const auto cleanPath = WriteFile(dir.path / "clean.txt", "use old_name\n");
    const auto dirtyPath = WriteFile(dir.path / "dirty.txt", "old_name again\n");

    Buffer& own   = fixture.bufferList.OpenOrCreateFile(ownPath);
    Buffer& dirty = fixture.bufferList.OpenOrCreateFile(dirtyPath);
    dirty.SetPoint(dirty.Content().ByteLength());
    dirty.InsertAtPoint("user typing");
    // clean.txt is deliberately not open: the rename opens it.

    std::vector<std::string> labels;
    fixture.registry.SetEditorHooks(ApplyingHooks(fixture.bufferList, labels));

    ned::editor::lsp::Client* client = nullptr;
    auto                      server = ned::test::FakeLspServer::Create(fixture.lspManager, "test-lang", fixture.eventLoop, client);
    fixture.lspManager.SyncBuffer(own, "test-lang");

    bool invoked = false;
    Json outcome;
    fixture.registry.CallTool("rename_symbol", Json{{"file", ownPath.string()}, {"line", 1}, {"column", 1}, {"newName", "new_name"}},
                              [&](Json result) {
                                  invoked = true;
                                  REQUIRE_FALSE(IsError(result));
                                  outcome = Json::parse(ResultText(result));
                              });

    const Json request = server.ReadRequest("textDocument/rename");
    REQUIRE(request["params"]["newName"] == "new_name");
    client->DispatchFrame(Response(request, {{"changes",
                                              {
                                                  {"file://" + ownPath.string(), Json::array({ReplaceEdit(0, 0, 8, "new_name")})},
                                                  {"file://" + cleanPath.string(), Json::array({ReplaceEdit(0, 4, 12, "new_name")})},
                                                  {"file://" + dirtyPath.string(), Json::array({ReplaceEdit(0, 0, 8, "new_name")})},
                                              }}})
                              .dump());

    REQUIRE(invoked);
    REQUIRE(labels == std::vector<std::string>{"Rename to new_name"});
    REQUIRE(ReadFile(ownPath) == "new_name here\n");
    REQUIRE(ReadFile(cleanPath) == "use new_name\n");
    REQUIRE_FALSE(own.Modified());

    // The user's own unsaved edit is never written behind their back.
    REQUIRE(ReadFile(dirtyPath) == "old_name again\n");
    REQUIRE(dirty.Modified());
    REQUIRE(dirty.Text() == "new_name again\nuser typing");

    REQUIRE(outcome.at("saved").size() == 2);
    REQUIRE(outcome.at("unsavedBecauseUserHadEdits") == Json::array({dirtyPath.string()}));
    REQUIRE_FALSE(outcome.contains("saveFailed"));
}

TEST_CASE("rename_symbol reports an edit the pane refused as an error", "[Mcp]") {
    Fixture    fixture;
    TempDir    dir;
    const auto path   = WriteFile(dir.path / "own.txt", "old_name\n");
    Buffer&    buffer = fixture.bufferList.OpenOrCreateFile(path);
    fixture.registry.SetEditorHooks({.applyWorkspaceEdit = [](const ResolvedRename&, const std::string&) { return false; }});

    ned::editor::lsp::Client* client = nullptr;
    auto                      server = ned::test::FakeLspServer::Create(fixture.lspManager, "test-lang", fixture.eventLoop, client);
    fixture.lspManager.SyncBuffer(buffer, "test-lang");

    bool invoked = false;
    fixture.registry.CallTool("rename_symbol", Json{{"file", path.string()}, {"line", 1}, {"column", 1}, {"newName", "new_name"}},
                              [&](Json result) {
                                  invoked = true;
                                  REQUIRE(IsError(result));
                                  REQUIRE(ResultText(result).find("was not applied") != std::string::npos);
                              });
    const Json request = server.ReadRequest("textDocument/rename");
    client->DispatchFrame(
        Response(request, {{"changes", {{"file://" + path.string(), Json::array({ReplaceEdit(0, 0, 8, "new_name")})}}}}).dump());

    REQUIRE(invoked);
    REQUIRE(ReadFile(path) == "old_name\n");
}

TEST_CASE("apply_code_action applies the action with the given title and saves the file", "[Mcp]") {
    Fixture    fixture;
    TempDir    dir;
    BackupsOff backupsOff;
    const auto path   = WriteFile(dir.path / "own.txt", "bad_code\n");
    Buffer&    buffer = fixture.bufferList.OpenOrCreateFile(path);

    std::vector<std::string> labels;
    fixture.registry.SetEditorHooks(ApplyingHooks(fixture.bufferList, labels));

    ned::editor::lsp::Client* client = nullptr;
    auto                      server = ned::test::FakeLspServer::Create(fixture.lspManager, "test-lang", fixture.eventLoop, client);
    fixture.lspManager.SyncBuffer(buffer, "test-lang");

    const std::string uri     = "file://" + path.string();
    const Json        actions = Json::array({
        {{"title", "Something else"}, {"edit", {{"changes", {{uri, Json::array({ReplaceEdit(0, 0, 3, "odd")})}}}}}},
        {{"title", "Fix the code"}, {"edit", {{"changes", {{uri, Json::array({ReplaceEdit(0, 0, 3, "good")})}}}}}},
    });

    SECTION("an exact title is applied") {
        bool invoked = false;
        fixture.registry.CallTool("apply_code_action", Json{{"file", path.string()}, {"line", 1}, {"column", 1}, {"title", "Fix the code"}},
                                  [&](Json result) {
                                      invoked = true;
                                      REQUIRE_FALSE(IsError(result));
                                  });
        client->DispatchFrame(Response(server.ReadRequest("textDocument/codeAction"), actions).dump());

        REQUIRE(invoked);
        REQUIRE(labels == std::vector<std::string>{"Fix the code"});
        REQUIRE(ReadFile(path) == "good_code\n");
    }

    SECTION("an unknown title applies nothing and lists what is available") {
        bool invoked = false;
        fixture.registry.CallTool("apply_code_action", Json{{"file", path.string()}, {"line", 1}, {"column", 1}, {"title", "Fix"}},
                                  [&](Json result) {
                                      invoked = true;
                                      REQUIRE(IsError(result));
                                      REQUIRE(ResultText(result).find("\"Fix the code\"") != std::string::npos);
                                  });
        client->DispatchFrame(Response(server.ReadRequest("textDocument/codeAction"), actions).dump());

        REQUIRE(invoked);
        REQUIRE(labels.empty());
        REQUIRE(ReadFile(path) == "bad_code\n");
    }
}

TEST_CASE("apply_code_action resolves an edit-less action, applies it, then runs its command", "[Mcp]") {
    Fixture    fixture;
    TempDir    dir;
    BackupsOff backupsOff;
    const auto path   = WriteFile(dir.path / "own.txt", "bad_code\n");
    Buffer&    buffer = fixture.bufferList.OpenOrCreateFile(path);

    std::vector<std::string> labels;
    fixture.registry.SetEditorHooks(ApplyingHooks(fixture.bufferList, labels));

    ned::editor::lsp::Client* client = nullptr;
    auto                      server = ned::test::FakeLspServer::Create(fixture.lspManager, "test-lang", fixture.eventLoop, client);
    fixture.lspManager.SyncBuffer(buffer, "test-lang");

    bool invoked = false;
    Json outcome;
    fixture.registry.CallTool("apply_code_action", Json{{"file", path.string()}, {"line", 1}, {"column", 1}, {"title", "Fix and log"}},
                              [&](Json result) {
                                  invoked = true;
                                  REQUIRE_FALSE(IsError(result));
                                  outcome = Json::parse(ResultText(result));
                              });

    const Json unresolved = {{"title", "Fix and log"}, {"kind", "quickfix"}, {"data", 7}};
    client->DispatchFrame(Response(server.ReadRequest("textDocument/codeAction"), Json::array({unresolved})).dump());
    // An action whose command is listed but whose edit isn't yet.
    const Json resolveRequest = server.ReadRequest("codeAction/resolve");
    REQUIRE(resolveRequest["params"]["data"] == 7);
    Json resolved       = unresolved;
    resolved["edit"]    = {{"changes", {{"file://" + path.string(), Json::array({ReplaceEdit(0, 0, 3, "good")})}}}};
    resolved["command"] = {{"title", "log"}, {"command", "fixer.log"}, {"arguments", Json::array({1})}};
    client->DispatchFrame(Response(resolveRequest, resolved).dump());

    REQUIRE_FALSE(invoked); // still waiting on the command
    REQUIRE(ReadFile(path) == "good_code\n");

    const Json commandRequest = server.ReadRequest("workspace/executeCommand");
    REQUIRE(commandRequest["params"]["command"] == "fixer.log");
    client->DispatchFrame(Response(commandRequest, nullptr).dump());

    REQUIRE(invoked);
    REQUIRE(outcome.at("applied") == "Fix and log");
    REQUIRE(outcome.at("command") == Json{{"name", "fixer.log"}, {"succeeded", true}});
}

TEST_CASE("goto_location checks the file, then defers to the editor's decision to move", "[Mcp]") {
    Fixture    fixture;
    TempDir    dir;
    const auto path = WriteFile(dir.path / "shown.txt", "one\ntwo\n");

    const auto call = [&](const Json& args) {
        Json got;
        fixture.registry.CallTool("goto_location", args, [&](Json result) { got = std::move(result); });
        REQUIRE_FALSE(got.is_null());
        return got;
    };

    REQUIRE(ResultText(call(Json{{"file", path.string()}, {"line", 2}})).find("isn't available") != std::string::npos);

    std::optional<std::pair<std::filesystem::path, std::size_t>> visited;
    bool                                                         allow = true;
    fixture.registry.SetEditorHooks({.visitLocation = [&](const std::filesystem::path& target, std::size_t line) {
        if (allow) {
            visited = {target, line};
        }
        return allow;
    }});

    REQUIRE(IsError(call(Json{{"file", (dir.path / "missing.txt").string()}, {"line", 1}})));
    REQUIRE(IsError(call(Json{{"file", path.string()}, {"line", 0}})));
    REQUIRE_FALSE(visited);

    REQUIRE_FALSE(IsError(call(Json{{"file", path.string()}, {"line", 2}})));
    REQUIRE(visited);
    REQUIRE(visited->first == std::filesystem::weakly_canonical(path));
    REQUIRE(visited->second == 2);

    allow               = false;
    const Json declined = call(Json{{"file", path.string()}, {"line", 1}});
    REQUIRE_FALSE(IsError(declined));
    REQUIRE(ResultText(declined).find("Not shown") != std::string::npos);
}
