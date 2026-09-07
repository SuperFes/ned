#include "McpToolRegistry.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <system_error>

#include "Editor/Dap/DapManager.h"
#include "Editor/DiagnosticsLog.h"
#include "Editor/Lsp/LspContent.h"
#include "Editor/Lsp/LspEditApply.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Lsp/LspPosition.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectSearch.h"
#include "Editor/TestRun/TestResult.h"
#include "Editor/TestRun/TestRunner.h"
#include "Editor/Vcs/VcsProvider.h"
#include "Editor/Vcs/VcsRunner.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor::mcp {

namespace {

    // Result caps, so a pathological search/test run can't hand an agent a
    // multi-megabyte tool result -- v1 safety cut, not a general pagination
    // scheme (MCP's own tools/list pagination is a different, unrelated
    // cursor mechanism -- see the "search_project"/"get_test_results"
    // handlers below).
    constexpr std::size_t kMaxSearchResults = 200;
    constexpr std::size_t kMaxTestResults   = 500;
    constexpr std::size_t kMaxLogEntries    = 200;

    std::string SeverityName(text::Buffer::Diagnostic::Severity severity) {
        switch (severity) {
            case text::Buffer::Diagnostic::Severity::Error:
                return "error";
            case text::Buffer::Diagnostic::Severity::Warning:
                return "warning";
            case text::Buffer::Diagnostic::Severity::Information:
                return "information";
            case text::Buffer::Diagnostic::Severity::Hint:
                return "hint";
        }
        return "unknown";
    }

    std::string TestStatusName(testrun::TestResult::Status status) {
        switch (status) {
            case testrun::TestResult::Status::Passed:
                return "passed";
            case testrun::TestResult::Status::Failed:
                return "failed";
            case testrun::TestResult::Status::Skipped:
                return "skipped";
        }
        return "unknown";
    }

    std::string LogSeverityName(LogSeverity severity) {
        switch (severity) {
            case LogSeverity::Info:
                return "info";
            case LogSeverity::Warning:
                return "warning";
            case LogSeverity::Error:
                return "error";
        }
        return "unknown";
    }

    // "YYYY-MM-DD HH:MM:SS" local time -- a plain, unambiguous stand-in for
    // DiagnosticsLog.cpp's own FormatLine timestamp rendering (not exported
    // for reuse; this is a small enough duplicate to not warrant extracting).
    std::string FormatLogTimestamp(std::chrono::system_clock::time_point timestamp) {
        const std::time_t seconds = std::chrono::system_clock::to_time_t(timestamp);
        std::tm            local{};
        localtime_r(&seconds, &local);
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
        return buffer;
    }

    // A relative `file` argument is resolved against ProjectRoot() -- every
    // other file-shaped argument this codebase's own commands accept follows
    // the same convention (ProjectSearch/ProjectFileOps, etc.). An absolute
    // path is used verbatim. weakly_canonical mirrors BufferList::OpenFile's
    // own dedupe-by-path comparison (Text/BufferList.h) so a tool-provided
    // path matches an already-open buffer the same way a real find-file
    // would, even across a "./"-prefixed or symlinked spelling.
    std::filesystem::path ResolveArgPath(const std::string& file) {
        std::filesystem::path path(file);
        if (path.is_relative()) {
            path = editor::ProjectRoot() / path;
        }
        std::error_code ec;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        return ec ? path : canonical;
    }

    // Every handler below needs an already-open Buffer -- LSP requests only
    // resolve against a buffer LspManager has already synced at least once
    // (see LspManager.h's own header comment: every open buffer is synced,
    // on the active pane's own per-frame Paint() or the periodic background
    // tick for every other one), so "find it in BufferList" is the right
    // (and only) v1 resolution, not "open it fresh" -- a tool asking about a
    // file the human hasn't opened in ned gets a clear error instead of
    // silently spawning a new background buffer/LSP sync behind their back.
    text::Buffer* FindOpenBuffer(text::BufferList& bufferList, const std::string& file) {
        return bufferList.FindByPath(ResolveArgPath(file));
    }

    std::optional<std::string> RequireString(const Json& args, const char* key) {
        if (!args.contains(key) || !args[key].is_string()) {
            return std::nullopt;
        }
        return args[key].get<std::string>();
    }

    std::optional<long long> RequireInt(const Json& args, const char* key) {
        if (!args.contains(key) || !args[key].is_number_integer()) {
            return std::nullopt;
        }
        return args[key].get<long long>();
    }

} // namespace

Json MakeTextToolResult(std::string text, bool isError) {
    return Json{
        {"content", Json::array({Json{{"type", "text"}, {"text", std::move(text)}}})},
        {"isError", isError},
    };
}

ToolRegistry::ToolRegistry(text::BufferList& bufferList, lsp::LspManager& lspManager, vcs::VcsRunner& vcsRunner, testrun::TestRunner& testRunner,
                           dap::DapManager& dapManager) : bufferList_(bufferList), lspManager_(lspManager), vcsRunner_(vcsRunner), testRunner_(testRunner), dapManager_(dapManager) {
    RegisterBuiltinTools();
}

void ToolRegistry::RegisterTool(std::string name, std::string description, Json inputSchema, Handler handler) {
    entries_.push_back(Entry{
        .descriptor = ToolDescriptor{.name = std::move(name), .description = std::move(description), .inputSchema = std::move(inputSchema)},
        .handler    = std::move(handler),
    });
}

std::vector<ToolDescriptor> ToolRegistry::ListTools() const {
    std::vector<ToolDescriptor> tools;
    tools.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        tools.push_back(entry.descriptor);
    }
    return tools;
}

bool ToolRegistry::HasTool(const std::string& name) const {
    return std::any_of(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.descriptor.name == name; });
}

void ToolRegistry::CallTool(const std::string& name, const Json& arguments, ResultCallback callback) const {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.descriptor.name == name; });
    if (it == entries_.end()) {
        callback(MakeTextToolResult("Unknown tool: " + name, /*isError=*/true));
        return;
    }
    it->handler(arguments, callback);
}

void ToolRegistry::RegisterBuiltinTools() {
    RegisterTool(
        "get_diagnostics", "Get the LSP diagnostics (errors/warnings) currently known for a file already open in ned.",
        Json{
            {"type", "object"},
            {"properties", {{"file", {{"type", "string"}, {"description", "Path to the file, absolute or relative to the project root."}}}}},
            {"required", Json::array({"file"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto file = RequireString(args, "file");
            if (!file) {
                callback(MakeTextToolResult("Missing required argument: file", true));
                return;
            }
            text::Buffer* buffer = FindOpenBuffer(bufferList_, *file);
            if (!buffer) {
                callback(MakeTextToolResult("File is not open in ned: " + *file, true));
                return;
            }
            Json diagnostics = Json::array();
            for (const text::Buffer::Diagnostic& diagnostic : buffer->Diagnostics()) {
                const lsp::LspPosition pos = lsp::BytePositionToLsp(buffer->Content(), diagnostic.startByte);
                diagnostics.push_back(Json{
                    {"line", pos.line + 1},
                    {"column", pos.character + 1},
                    {"severity", SeverityName(diagnostic.severity)},
                    {"message", diagnostic.message},
                });
            }
            callback(MakeTextToolResult(Json{{"file", *file}, {"diagnostics", diagnostics}}.dump()));
        });

    RegisterTool(
        "hover", "Get LSP hover information (type/doc info) at a position in a file already open in ned.",
        Json{
            {"type", "object"},
            {"properties",
             {{"file", {{"type", "string"}, {"description", "Path to the file, absolute or relative to the project root."}}},
              {"line", {{"type", "integer"}, {"description", "1-indexed line number."}}},
              {"column", {{"type", "integer"}, {"description", "1-indexed column (UTF-16 code unit offset within the line)."}}}}},
            {"required", Json::array({"file", "line", "column"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto file = RequireString(args, "file");
            const auto line = RequireInt(args, "line");
            const auto col  = RequireInt(args, "column");
            if (!file || !line || !col) {
                callback(MakeTextToolResult("Missing required argument(s): file, line, column", true));
                return;
            }
            text::Buffer* buffer = FindOpenBuffer(bufferList_, *file);
            if (!buffer) {
                callback(MakeTextToolResult("File is not open in ned: " + *file, true));
                return;
            }
            const std::size_t byteOffset =
                lsp::LspPositionToByte(buffer->Content(), lsp::LspPosition{static_cast<std::size_t>(*line - 1), static_cast<std::size_t>(*col - 1)});
            lspManager_.RequestHover(*buffer, byteOffset, [callback](std::optional<std::string> text) {
                callback(MakeTextToolResult(text.value_or("No hover information available.")));
            });
        });

    // goto_definition and find_references share the exact same argument
    // shape and result shape (LspManager::ResolvedLocation list) -- one
    // shared lambda factory rather than two near-identical handler bodies.
    auto registerLocationTool = [this](std::string name, std::string description, std::string emptyMessage, auto requestMember) {
        RegisterTool(
            name, description,
            Json{
                {"type", "object"},
                {"properties",
                 {{"file", {{"type", "string"}, {"description", "Path to the file, absolute or relative to the project root."}}},
                  {"line", {{"type", "integer"}, {"description", "1-indexed line number."}}},
                  {"column", {{"type", "integer"}, {"description", "1-indexed column (UTF-16 code unit offset within the line)."}}}}},
                {"required", Json::array({"file", "line", "column"})},
            },
            [this, emptyMessage, requestMember](const Json& args, const ResultCallback& callback) {
                const auto file = RequireString(args, "file");
                const auto line = RequireInt(args, "line");
                const auto col  = RequireInt(args, "column");
                if (!file || !line || !col) {
                    callback(MakeTextToolResult("Missing required argument(s): file, line, column", true));
                    return;
                }
                text::Buffer* buffer = FindOpenBuffer(bufferList_, *file);
                if (!buffer) {
                    callback(MakeTextToolResult("File is not open in ned: " + *file, true));
                    return;
                }
                const std::size_t byteOffset = lsp::LspPositionToByte(
                    buffer->Content(), lsp::LspPosition{static_cast<std::size_t>(*line - 1), static_cast<std::size_t>(*col - 1)});
                // Calling through a pointer-to-member drops RequestDefinition/
                // RequestReferences' own default serverKey argument (default
                // arguments aren't part of a function pointer's type) --
                // pass it explicitly.
                (lspManager_.*requestMember)(*buffer, byteOffset, [callback, emptyMessage](std::vector<lsp::LspManager::ResolvedLocation> locations) {
                    if (locations.empty()) {
                        callback(MakeTextToolResult(emptyMessage));
                        return;
                    }
                    Json results = Json::array();
                    for (const lsp::LspManager::ResolvedLocation& location : locations) {
                        results.push_back(Json{
                            {"file", location.path.string()},
                            {"line", location.position.line + 1},
                            {"column", location.position.character + 1},
                        });
                    }
                    callback(MakeTextToolResult(results.dump()));
                },
                std::string{});
            });
    };
    registerLocationTool("goto_definition", "Find the definition location(s) of the symbol at a position in a file already open in ned.",
                         "No definition found.", &lsp::LspManager::RequestDefinition);
    registerLocationTool("find_references", "Find every reference to the symbol at a position in a file already open in ned.",
                         "No references found.", &lsp::LspManager::RequestReferences);

    RegisterTool(
        "git_status", "Get the current git working-tree status (changed/staged/untracked files) for the project.", Json{{"type", "object"}, {"properties", Json::object()}},
        [this](const Json&, const ResultCallback& callback) {
            vcsRunner_.RequestStatus(
                [callback](std::vector<vcs::VcsStatusEntry> entries) {
                    Json results = Json::array();
                    for (const vcs::VcsStatusEntry& entry : entries) {
                        results.push_back(Json{{"state", entry.state}, {"path", entry.path}});
                    }
                    callback(MakeTextToolResult(results.dump()));
                },
                [callback](const std::string& error) { callback(MakeTextToolResult("git status failed: " + error, true)); });
        });

    RegisterTool(
        "git_diff", "Get the raw unified diff for one file's unstaged changes, or the whole working tree if no file is given.",
        Json{
            {"type", "object"},
            {"properties", {{"file", {{"type", "string"}, {"description", "Optional: path to one file, absolute or relative to the project root."}}}}},
        },
        [this](const Json& args, const ResultCallback& callback) {
            auto onComplete = [callback](std::string rawDiff) { callback(MakeTextToolResult(rawDiff.empty() ? "No changes." : rawDiff)); };
            auto onError    = [callback](const std::string& error) { callback(MakeTextToolResult("git diff failed: " + error, true)); };
            if (const auto file = RequireString(args, "file")) {
                vcsRunner_.RequestFileDiffText(ResolveArgPath(*file), /*staged=*/false, onComplete, onError);
            }
            else {
                vcsRunner_.RequestFullDiff(onComplete, onError);
            }
        });

    RegisterTool(
        "search_project", "Search every non-ignored, non-binary file under the project root for a regular expression (RE2 syntax).",
        Json{
            {"type", "object"},
            {"properties", {{"pattern", {{"type", "string"}, {"description", "RE2-syntax regular expression."}}}}},
            {"required", Json::array({"pattern"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto pattern = RequireString(args, "pattern");
            if (!pattern) {
                callback(MakeTextToolResult("Missing required argument: pattern", true));
                return;
            }
            std::vector<SearchMatch> matches;
            try {
                matches = SearchDirectory(editor::ProjectRoot(), *pattern);
            }
            catch (const SearchPatternError& e) {
                callback(MakeTextToolResult(std::string("Invalid search pattern: ") + e.what(), true));
                return;
            }
            const bool  truncated = matches.size() > kMaxSearchResults;
            Json        results   = Json::array();
            for (std::size_t i = 0; i < matches.size() && i < kMaxSearchResults; ++i) {
                results.push_back(Json{{"file", matches[i].file.string()}, {"line", matches[i].lineNumber}, {"text", matches[i].lineText}});
            }
            callback(MakeTextToolResult(Json{{"matches", results}, {"totalMatches", matches.size()}, {"truncated", truncated}}.dump()));
        });

    RegisterTool(
        "run_tests", "Start a test run (all tests, or filtered to one test name) using the project's configured test command. Returns immediately -- call get_test_results afterward to see the outcome.",
        Json{
            {"type", "object"},
            {"properties", {{"filter", {{"type", "string"}, {"description", "Optional: run only the test with this exact name."}}}}},
        },
        [this](const Json& args, const ResultCallback& callback) {
            if (const auto filter = RequireString(args, "filter")) {
                testRunner_.RunFiltered(*filter, "");
            }
            else {
                testRunner_.RunAll();
            }
            callback(MakeTextToolResult("Test run started. Call get_test_results to check the outcome."));
        });

    RegisterTool(
        "get_test_results", "Get the outcome of the most recent test run started via run_tests (or ned's own run-tests command).", Json{{"type", "object"}, {"properties", Json::object()}},
        [this](const Json&, const ResultCallback& callback) {
            const std::optional<testrun::TestRunOutcome>& outcome = testRunner_.LatestOutcome();
            if (!outcome) {
                callback(MakeTextToolResult("No test results yet -- call run_tests first."));
                return;
            }
            if (!outcome->parsedOk) {
                callback(MakeTextToolResult("Test output did not parse as format \"" + outcome->format + "\".", true));
                return;
            }
            Json results    = Json::array();
            const bool truncated = outcome->results.size() > kMaxTestResults;
            for (std::size_t i = 0; i < outcome->results.size() && i < kMaxTestResults; ++i) {
                const testrun::TestResult& result = outcome->results[i];
                results.push_back(Json{
                    {"name", result.name},
                    {"status", TestStatusName(result.status)},
                    {"file", result.file},
                    {"line", result.line},
                    {"message", result.message},
                });
            }
            callback(MakeTextToolResult(Json{
                                             {"format", outcome->format},
                                             {"passed", outcome->passed},
                                             {"failed", outcome->failed},
                                             {"skipped", outcome->skipped},
                                             {"failuresOnly", outcome->failuresOnly},
                                             {"results", results},
                                             {"truncated", truncated},
                                         }
                                             .dump()));
        });

    // acp-mcp-tool-bridge-remainder follow-up. Every tool below still reuses
    // an existing manager call unchanged -- the only new shared code this
    // slice needed was extracting Editor/Lsp/LspEditApply.h out of
    // BufferView.cpp (format_buffer's own apply step). Two things were
    // deliberately NOT added here after checking the real APIs against
    // ROADMAP's own aspirational list:
    //   - rename_symbol/code_actions stay listing/preview-only
    //     (preview_rename, code_actions below) rather than actually
    //     applying anything: a real apply needs BufferView::ApplyProjectEdit's
    //     multi-file transaction machinery (ProjectUndoManager recording,
    //     file create/rename/delete via DocumentChangeOp) -- genuinely
    //     BufferView/WindowManager-coupled, not a thin wrapper the way
    //     everything else here is. Same open gap as goto(file,line)
    //     navigation, not attempted in this slice either.
    //   - Org capture_note was dropped entirely: OrgCapture::InsertCapture
    //     only supports a template whose text is fixed at Janet-registration
    //     time ("%?" just marks where point lands after expansion) -- there
    //     is no way to inject agent-supplied free text into a capture
    //     through the existing API, so a faithful capture_note tool needs a
    //     small OrgCapture.h capability addition first, not just a wrapper.

    RegisterTool(
        "git_stage", "Stage a file's changes for the next commit.",
        Json{
            {"type", "object"},
            {"properties", {{"file", {{"type", "string"}, {"description", "Path to the file, absolute or relative to the project root."}}}}},
            {"required", Json::array({"file"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto file = RequireString(args, "file");
            if (!file) {
                callback(MakeTextToolResult("Missing required argument: file", true));
                return;
            }
            vcsRunner_.RequestStage(
                ResolveArgPath(*file), [callback, file] { callback(MakeTextToolResult("Staged " + *file + ".")); },
                [callback](const std::string& error) { callback(MakeTextToolResult("git stage failed: " + error, true)); });
        });

    RegisterTool(
        "git_unstage", "Unstage a file (keep its changes, remove them from the next commit).",
        Json{
            {"type", "object"},
            {"properties", {{"file", {{"type", "string"}, {"description", "Path to the file, absolute or relative to the project root."}}}}},
            {"required", Json::array({"file"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto file = RequireString(args, "file");
            if (!file) {
                callback(MakeTextToolResult("Missing required argument: file", true));
                return;
            }
            vcsRunner_.RequestUnstage(
                ResolveArgPath(*file), [callback, file] { callback(MakeTextToolResult("Unstaged " + *file + ".")); },
                [callback](const std::string& error) { callback(MakeTextToolResult("git unstage failed: " + error, true)); });
        });

    RegisterTool(
        "git_commit", "Commit the currently staged changes with the given message.",
        Json{
            {"type", "object"},
            {"properties", {{"message", {{"type", "string"}, {"description", "The commit message."}}}}},
            {"required", Json::array({"message"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto message = RequireString(args, "message");
            if (!message) {
                callback(MakeTextToolResult("Missing required argument: message", true));
                return;
            }
            vcsRunner_.RequestCommit(
                *message, [callback](std::string summary) { callback(MakeTextToolResult(summary.empty() ? "Committed." : summary)); },
                [callback](const std::string& error) { callback(MakeTextToolResult("git commit failed: " + error, true)); });
        });

    RegisterTool(
        "git_branch_list", "List every local branch, marking the currently checked-out one.", Json{{"type", "object"}, {"properties", Json::object()}},
        [this](const Json&, const ResultCallback& callback) {
            vcsRunner_.RequestBranchList(
                [callback](std::vector<vcs::VcsBranchEntry> branches) {
                    Json results = Json::array();
                    for (const vcs::VcsBranchEntry& branch : branches) {
                        results.push_back(Json{{"name", branch.name}, {"current", branch.current}});
                    }
                    callback(MakeTextToolResult(results.dump()));
                },
                [callback](const std::string& error) { callback(MakeTextToolResult("git branch list failed: " + error, true)); });
        });

    RegisterTool(
        "git_branch_switch", "Switch (checkout) to an existing local branch.",
        Json{
            {"type", "object"},
            {"properties", {{"name", {{"type", "string"}, {"description", "The branch name to switch to."}}}}},
            {"required", Json::array({"name"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto name = RequireString(args, "name");
            if (!name) {
                callback(MakeTextToolResult("Missing required argument: name", true));
                return;
            }
            vcsRunner_.RequestBranchSwitch(
                *name, [callback, name] { callback(MakeTextToolResult("Switched to branch " + *name + ".")); },
                [callback](const std::string& error) { callback(MakeTextToolResult("git branch switch failed: " + error, true)); });
        });

    RegisterTool(
        "git_blame", "Get the git blame info (commit/author/date/summary) for one line of a file already open in ned.",
        Json{
            {"type", "object"},
            {"properties",
             {{"file", {{"type", "string"}, {"description", "Path to the file, absolute or relative to the project root."}}},
              {"line", {{"type", "integer"}, {"description", "1-indexed line number."}}}}},
            {"required", Json::array({"file", "line"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto file = RequireString(args, "file");
            const auto line = RequireInt(args, "line");
            if (!file || !line) {
                callback(MakeTextToolResult("Missing required argument(s): file, line", true));
                return;
            }
            text::Buffer* buffer = FindOpenBuffer(bufferList_, *file);
            if (!buffer) {
                callback(MakeTextToolResult("File is not open in ned: " + *file, true));
                return;
            }
            const std::size_t lineIndex = static_cast<std::size_t>(*line - 1);
            vcsRunner_.RequestBlame(
                *buffer,
                [callback, lineIndex](std::vector<vcs::VcsBlameLine> lines) {
                    if (lineIndex >= lines.size()) {
                        callback(MakeTextToolResult("Line out of range for this file's blame.", true));
                        return;
                    }
                    const vcs::VcsBlameLine& blameLine = lines[lineIndex];
                    callback(MakeTextToolResult(Json{
                                                     {"commitHash", blameLine.commitHash},
                                                     {"author", blameLine.author},
                                                     {"date", blameLine.date},
                                                     {"summary", blameLine.summary},
                                                 }
                                                     .dump()));
                },
                [callback](const std::string& error) { callback(MakeTextToolResult("git blame failed: " + error, true)); });
        });

    RegisterTool(
        "rerun_failed_tests",
        "Rerun every test that failed in the most recent run. Returns immediately -- call get_test_results afterward to see the outcome.",
        Json{{"type", "object"}, {"properties", Json::object()}},
        [this](const Json&, const ResultCallback& callback) {
            const std::size_t queued = testRunner_.RerunFailed();
            if (queued == 0) {
                callback(MakeTextToolResult("No failed tests to rerun (or no test command configured)."));
                return;
            }
            callback(MakeTextToolResult("Queued " + std::to_string(queued) + " failed test(s) for rerun. Call get_test_results to check the outcome."));
        });

    RegisterTool(
        "workspace_symbols", "Search the project's whole workspace for symbols matching a query, via the LSP server for the given file's language.",
        Json{
            {"type", "object"},
            {"properties",
             {{"file", {{"type", "string"}, {"description", "Path to a file already open in ned, used to pick which language server to ask."}}},
              {"query", {{"type", "string"}, {"description", "The symbol name (or substring) to search for."}}}}},
            {"required", Json::array({"file", "query"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto file  = RequireString(args, "file");
            const auto query = RequireString(args, "query");
            if (!file || !query) {
                callback(MakeTextToolResult("Missing required argument(s): file, query", true));
                return;
            }
            text::Buffer* buffer = FindOpenBuffer(bufferList_, *file);
            if (!buffer) {
                callback(MakeTextToolResult("File is not open in ned: " + *file, true));
                return;
            }
            lspManager_.RequestWorkspaceSymbols(*buffer, *query, [callback](std::vector<lsp::LspManager::SymbolResult> symbols) {
                if (symbols.empty()) {
                    callback(MakeTextToolResult("No symbols found."));
                    return;
                }
                Json results = Json::array();
                for (const lsp::LspManager::SymbolResult& symbol : symbols) {
                    results.push_back(Json{
                        {"name", symbol.name},
                        {"containerName", symbol.containerName},
                        {"kind", symbol.kind},
                        {"file", symbol.path.string()},
                        {"line", symbol.position.line + 1},
                        {"column", symbol.position.character + 1},
                    });
                }
                callback(MakeTextToolResult(results.dump()));
            });
        });

    RegisterTool(
        "format_buffer", "Format a file already open in ned using its LSP server, applying the result as one undoable edit.",
        Json{
            {"type", "object"},
            {"properties", {{"file", {{"type", "string"}, {"description", "Path to the file, absolute or relative to the project root."}}}}},
            {"required", Json::array({"file"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto file = RequireString(args, "file");
            if (!file) {
                callback(MakeTextToolResult("Missing required argument: file", true));
                return;
            }
            text::Buffer* buffer = FindOpenBuffer(bufferList_, *file);
            if (!buffer) {
                callback(MakeTextToolResult("File is not open in ned: " + *file, true));
                return;
            }
            lspManager_.RequestFormatting(*buffer, [this, callback, buffer, file = *file](std::optional<std::vector<lsp::WorkspaceTextEdit>> edits) {
                if (!edits) {
                    callback(MakeTextToolResult("Formatting failed or is not supported for this buffer.", true));
                    return;
                }
                if (edits->empty()) {
                    callback(MakeTextToolResult("Already formatted -- no changes."));
                    return;
                }
                // buffer is a stale pointer by the time this fires if the
                // human closed it while the request was in flight -- never
                // dereferenced until re-confirmed still open at this exact
                // address, the same "opaque key, re-look-up before
                // dereferencing" idiom BufferView::RequestLspFormatThenSaveBuffer
                // uses for this identical hazard.
                if (FindOpenBuffer(bufferList_, file) != buffer) {
                    callback(MakeTextToolResult("Buffer was closed before formatting completed.", true));
                    return;
                }
                const std::size_t count = edits->size();
                lsp::ApplyWorkspaceTextEdits(*buffer, *edits);
                callback(MakeTextToolResult("Applied " + std::to_string(count) + " formatting edit(s), as one undo step."));
            });
        });

    RegisterTool(
        "code_actions",
        "List the LSP code actions (quick fixes/refactorings) available at a position in a file already open in ned. Read-only -- "
        "lists titles only, does not apply any of them.",
        Json{
            {"type", "object"},
            {"properties",
             {{"file", {{"type", "string"}, {"description", "Path to the file, absolute or relative to the project root."}}},
              {"line", {{"type", "integer"}, {"description", "1-indexed line number."}}},
              {"column", {{"type", "integer"}, {"description", "1-indexed column (UTF-16 code unit offset within the line)."}}}}},
            {"required", Json::array({"file", "line", "column"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto file = RequireString(args, "file");
            const auto line = RequireInt(args, "line");
            const auto col  = RequireInt(args, "column");
            if (!file || !line || !col) {
                callback(MakeTextToolResult("Missing required argument(s): file, line, column", true));
                return;
            }
            text::Buffer* buffer = FindOpenBuffer(bufferList_, *file);
            if (!buffer) {
                callback(MakeTextToolResult("File is not open in ned: " + *file, true));
                return;
            }
            const std::size_t byteOffset = lsp::LspPositionToByte(
                buffer->Content(), lsp::LspPosition{static_cast<std::size_t>(*line - 1), static_cast<std::size_t>(*col - 1)});
            lspManager_.RequestCodeActions(*buffer, byteOffset, byteOffset, [callback](std::vector<lsp::CodeAction> actions) {
                if (actions.empty()) {
                    callback(MakeTextToolResult("No code actions available here."));
                    return;
                }
                Json results = Json::array();
                for (const lsp::CodeAction& action : actions) {
                    results.push_back(Json{{"title", action.title}, {"hasEdit", action.hasEdit}});
                }
                callback(MakeTextToolResult(results.dump()));
            });
        });

    RegisterTool(
        "preview_rename",
        "Preview an LSP rename at a position in a file already open in ned -- lists which files and how many edits would be "
        "touched. Read-only -- does not apply the rename; use ned's own lsp-rename command (or ask the user) to actually do it.",
        Json{
            {"type", "object"},
            {"properties",
             {{"file", {{"type", "string"}, {"description", "Path to the file, absolute or relative to the project root."}}},
              {"line", {{"type", "integer"}, {"description", "1-indexed line number."}}},
              {"column", {{"type", "integer"}, {"description", "1-indexed column (UTF-16 code unit offset within the line)."}}},
              {"newName", {{"type", "string"}, {"description", "The proposed new name for the symbol."}}}}},
            {"required", Json::array({"file", "line", "column", "newName"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto file    = RequireString(args, "file");
            const auto line    = RequireInt(args, "line");
            const auto col     = RequireInt(args, "column");
            const auto newName = RequireString(args, "newName");
            if (!file || !line || !col || !newName) {
                callback(MakeTextToolResult("Missing required argument(s): file, line, column, newName", true));
                return;
            }
            text::Buffer* buffer = FindOpenBuffer(bufferList_, *file);
            if (!buffer) {
                callback(MakeTextToolResult("File is not open in ned: " + *file, true));
                return;
            }
            const std::size_t byteOffset = lsp::LspPositionToByte(
                buffer->Content(), lsp::LspPosition{static_cast<std::size_t>(*line - 1), static_cast<std::size_t>(*col - 1)});
            lspManager_.RequestRename(*buffer, byteOffset, *newName, [callback](std::optional<lsp::LspManager::ResolvedRename> result) {
                if (!result || !result->hasEdit) {
                    callback(MakeTextToolResult("Rename failed, or is not supported at this position.", true));
                    return;
                }
                if (result->touchesUnsupportedForm) {
                    callback(MakeTextToolResult("The server's rename response used a form this tool can't preview.", true));
                    return;
                }
                Json files = Json::array();
                for (const lsp::LspManager::ResolvedRenameEdit& edit : result->edits) {
                    files.push_back(Json{{"file", edit.path.string()}, {"editCount", edit.edits.size()}});
                }
                for (const lsp::LspManager::ResolvedDocumentChangeOp& op : result->documentChangeOps) {
                    files.push_back(Json{{"file", op.path.string()}, {"editCount", op.edits.size()}});
                }
                callback(MakeTextToolResult(Json{{"preview", true}, {"filesTouched", files}}.dump()));
            });
        });

    RegisterTool(
        "get_diagnostics_log", "Get ned's own internal message log (LSP/DAP/ACP/VCS/task/subprocess errors and warnings, not source-file diagnostics).",
        Json{
            {"type", "object"},
            {"properties",
             {{"category",
               {{"type", "string"},
                {"description", "Optional: filter to one category (general, janet, lsp, dap, acp, vcs, task, subprocess -- case-insensitive)."}}}}},
        },
        [this](const Json& args, const ResultCallback& callback) {
            std::optional<LogCategory> filter;
            if (const auto categoryName = RequireString(args, "category")) {
                std::string lowered = *categoryName;
                std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return std::tolower(c); });
                filter = LogCategoryFromString(lowered);
                if (!filter) {
                    callback(MakeTextToolResult("Unknown log category: " + *categoryName, true));
                    return;
                }
            }
            Json        results   = Json::array();
            std::size_t total     = 0;
            for (const LogEntry& entry : LogEntries()) {
                if (filter && entry.category != *filter) {
                    continue;
                }
                ++total;
                if (results.size() >= kMaxLogEntries) {
                    continue;
                }
                Json jsonEntry = Json{
                    {"timestamp", FormatLogTimestamp(entry.timestamp)},
                    {"category", std::string(LogCategoryToString(entry.category))},
                    {"severity", LogSeverityName(entry.severity)},
                    {"message", entry.message},
                    {"count", entry.count},
                };
                if (entry.path) {
                    jsonEntry["path"] = *entry.path;
                }
                if (entry.line) {
                    jsonEntry["line"] = *entry.line;
                }
                results.push_back(std::move(jsonEntry));
            }
            callback(MakeTextToolResult(Json{{"entries", results}, {"totalMatching", total}, {"truncated", total > kMaxLogEntries}}.dump()));
        });

    // DAP<->ACP debugging bridge (ROADMAP.md's "Collaboration & AI" entry).
    // Every DapManager::Request*/status-string method already runs on the
    // main thread and answers gracefully (an empty result, or a short
    // explanatory status string) when there's no session/no adapter
    // response/an out-of-range argument, exactly like LspManager's own
    // Request* methods do -- so these are thin wrappers, the same shape as
    // every git_*/lsp tool above, not new DapManager capability. Debug
    // session state (breakpoints/stack/variables/watches) is inherently
    // single-session/single-focus, matching DapManager's own "one session at
    // a time" design, so no buffer/file resolution is needed the way the LSP
    // tools above need FindOpenBuffer -- an agent driving these acts on
    // whichever session/thread/frame the human's own DAP UI would.
    RegisterTool(
        "dap_list_breakpoints", "List every breakpoint currently set, across every file.", Json{{"type", "object"}, {"properties", Json::object()}},
        [this](const Json&, const ResultCallback& callback) {
            const auto all = dapManager_.AllBreakpoints();
            if (all.empty()) {
                callback(MakeTextToolResult("No breakpoints set."));
                return;
            }
            Json results = Json::array();
            for (const auto& [pathKey, breakpoints] : all) {
                for (const dap::DapManager::PersistedBreakpoint& bp : breakpoints) {
                    Json entry = Json{{"file", pathKey}, {"line", bp.line}};
                    if (!bp.condition.empty()) {
                        entry["condition"] = bp.condition;
                    }
                    if (!bp.hitCondition.empty()) {
                        entry["hitCondition"] = bp.hitCondition;
                    }
                    if (!bp.logMessage.empty()) {
                        entry["logMessage"] = bp.logMessage;
                    }
                    results.push_back(std::move(entry));
                }
            }
            callback(MakeTextToolResult(results.dump()));
        });

    RegisterTool(
        "dap_set_breakpoint",
        "Set (or update) a breakpoint at a source line. Creates a plain breakpoint if none exists there yet; an "
        "optional condition/hitCondition/logMessage narrows it (a logMessage turns it into a non-halting logpoint).",
        Json{
            {"type", "object"},
            {"properties",
             {{"file", {{"type", "string"}, {"description", "Path to the file, absolute or relative to the project root."}}},
              {"line", {{"type", "integer"}, {"description", "1-indexed line number."}}},
              {"condition", {{"type", "string"}, {"description", "Optional: only stop when this expression is truthy."}}},
              {"hitCondition",
               {{"type", "string"}, {"description", "Optional: only stop once this adapter-evaluated hit-count expression is satisfied (e.g. \"> 5\")."}}},
              {"logMessage",
               {{"type", "string"}, {"description", "Optional: turn this into a logpoint -- never halts, just logs this message when hit."}}}}},
            {"required", Json::array({"file", "line"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto file = RequireString(args, "file");
            const auto line = RequireInt(args, "line");
            if (!file || !line) {
                callback(MakeTextToolResult("Missing required argument(s): file, line", true));
                return;
            }
            const std::filesystem::path    path          = ResolveArgPath(*file);
            const auto                     lineNum       = static_cast<std::size_t>(*line);
            const std::vector<std::size_t> existingLines = dapManager_.BreakpointsForFile(path);
            const bool                     alreadySet    = std::find(existingLines.begin(), existingLines.end(), lineNum) != existingLines.end();
            if (!alreadySet) {
                dapManager_.ToggleBreakpoint(path, lineNum);
            }
            std::string status = "Breakpoint set at " + *file + ":" + std::to_string(lineNum) + ".";
            if (const auto condition = RequireString(args, "condition")) {
                status = dapManager_.SetBreakpointCondition(path, lineNum, *condition);
            }
            if (const auto hitCondition = RequireString(args, "hitCondition")) {
                status = dapManager_.SetBreakpointHitCondition(path, lineNum, *hitCondition);
            }
            if (const auto logMessage = RequireString(args, "logMessage")) {
                status = dapManager_.SetBreakpointLogMessage(path, lineNum, *logMessage);
            }
            callback(MakeTextToolResult(status));
        });

    RegisterTool(
        "dap_remove_breakpoint", "Remove the breakpoint at a source line, if one exists.",
        Json{
            {"type", "object"},
            {"properties",
             {{"file", {{"type", "string"}, {"description", "Path to the file, absolute or relative to the project root."}}},
              {"line", {{"type", "integer"}, {"description", "1-indexed line number."}}}}},
            {"required", Json::array({"file", "line"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto file = RequireString(args, "file");
            const auto line = RequireInt(args, "line");
            if (!file || !line) {
                callback(MakeTextToolResult("Missing required argument(s): file, line", true));
                return;
            }
            const std::filesystem::path    path          = ResolveArgPath(*file);
            const auto                     lineNum       = static_cast<std::size_t>(*line);
            const std::vector<std::size_t> existingLines = dapManager_.BreakpointsForFile(path);
            if (std::find(existingLines.begin(), existingLines.end(), lineNum) == existingLines.end()) {
                callback(MakeTextToolResult("No breakpoint at " + *file + ":" + std::to_string(lineNum) + "."));
                return;
            }
            dapManager_.ToggleBreakpoint(path, lineNum);
            callback(MakeTextToolResult("Removed breakpoint at " + *file + ":" + std::to_string(lineNum) + "."));
        });

    RegisterTool(
        "dap_continue",
        "Start a debug session for a language (if none is running yet) or continue a stopped one. Returns "
        "immediately with a short status -- the session lands Running or Stopped asynchronously; poll "
        "dap_get_current_location or ned's own status to see where it lands.",
        Json{
            {"type", "object"},
            {"properties",
             {{"language",
               {{"type", "string"}, {"description", "Language key to launch (see ned/set-dap-adapter/ned/set-dap-launch) -- only used when no session is currently running."}}}}},
        },
        [this](const Json& args, const ResultCallback& callback) { callback(MakeTextToolResult(dapManager_.StartOrContinue(RequireString(args, "language").value_or("")))); });

    RegisterTool("dap_pause", "Pause the running debuggee.", Json{{"type", "object"}, {"properties", Json::object()}},
                 [this](const Json&, const ResultCallback& callback) { callback(MakeTextToolResult(dapManager_.Pause())); });

    RegisterTool("dap_stop_session", "Stop the running debug session.", Json{{"type", "object"}, {"properties", Json::object()}},
                 [this](const Json&, const ResultCallback& callback) { callback(MakeTextToolResult(dapManager_.StopSession())); });

    RegisterTool("dap_step_over", "Step over the current line in the stopped debug session.", Json{{"type", "object"}, {"properties", Json::object()}},
                 [this](const Json&, const ResultCallback& callback) { callback(MakeTextToolResult(dapManager_.StepOver())); });

    RegisterTool("dap_step_into", "Step into the call on the current line in the stopped debug session.", Json{{"type", "object"}, {"properties", Json::object()}},
                 [this](const Json&, const ResultCallback& callback) { callback(MakeTextToolResult(dapManager_.StepInto())); });

    RegisterTool("dap_step_out", "Step out of the current function in the stopped debug session.", Json{{"type", "object"}, {"properties", Json::object()}},
                 [this](const Json&, const ResultCallback& callback) { callback(MakeTextToolResult(dapManager_.StepOut())); });

    RegisterTool(
        "dap_get_current_location", "Get where the debug session is currently stopped, if it's stopped.", Json{{"type", "object"}, {"properties", Json::object()}},
        [this](const Json&, const ResultCallback& callback) {
            const auto stop = dapManager_.CurrentStopKeyAndLine();
            if (!stop) {
                callback(MakeTextToolResult("Debug session is not currently stopped."));
                return;
            }
            callback(MakeTextToolResult(Json{{"file", stop->first}, {"line", stop->second}}.dump()));
        });

    RegisterTool(
        "dap_get_stack_trace", "Get the call stack of the stopped debug session.", Json{{"type", "object"}, {"properties", Json::object()}},
        [this](const Json&, const ResultCallback& callback) {
            dapManager_.RequestStackTrace([callback](std::vector<dap::DapManager::StackFrame> frames) {
                if (frames.empty()) {
                    callback(MakeTextToolResult("No stack available (is the session stopped?)."));
                    return;
                }
                Json results = Json::array();
                for (const dap::DapManager::StackFrame& frame : frames) {
                    Json entry = Json{{"frameId", frame.id}, {"name", frame.name}};
                    if (frame.path) {
                        entry["file"] = frame.path->string();
                        entry["line"] = frame.line;
                    }
                    results.push_back(std::move(entry));
                }
                callback(MakeTextToolResult(results.dump()));
            });
        });

    RegisterTool(
        "dap_get_scopes", "Get the variable scopes (locals, arguments, ...) for a stack frame, from dap_get_stack_trace's own frameId.",
        Json{
            {"type", "object"},
            {"properties", {{"frameId", {{"type", "integer"}, {"description", "A frameId from dap_get_stack_trace."}}}}},
            {"required", Json::array({"frameId"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto frameId = RequireInt(args, "frameId");
            if (!frameId) {
                callback(MakeTextToolResult("Missing required argument: frameId", true));
                return;
            }
            dapManager_.RequestScopes(static_cast<int>(*frameId), [callback](std::vector<dap::DapManager::Scope> scopes) {
                if (scopes.empty()) {
                    callback(MakeTextToolResult("No scopes available (is the session stopped?)."));
                    return;
                }
                Json results = Json::array();
                for (const dap::DapManager::Scope& scope : scopes) {
                    results.push_back(Json{{"name", scope.name}, {"variablesReference", scope.variablesReference}});
                }
                callback(MakeTextToolResult(results.dump()));
            });
        });

    RegisterTool(
        "dap_get_variables",
        "Get the variables in a scope or composite value, from dap_get_scopes' or a prior dap_get_variables' own variablesReference.",
        Json{
            {"type", "object"},
            {"properties",
             {{"variablesReference", {{"type", "integer"}, {"description", "From dap_get_scopes, or a composite variable's own variablesReference."}}},
              {"hex", {{"type", "boolean"}, {"description", "Optional: render numeric values in hex instead of decimal."}}}}},
            {"required", Json::array({"variablesReference"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto reference = RequireInt(args, "variablesReference");
            if (!reference) {
                callback(MakeTextToolResult("Missing required argument: variablesReference", true));
                return;
            }
            const bool hex = args.contains("hex") && args["hex"].is_boolean() && args["hex"].get<bool>();
            dapManager_.RequestVariables(
                static_cast<int>(*reference),
                [callback](std::vector<dap::DapManager::Variable> variables) {
                    if (variables.empty()) {
                        callback(MakeTextToolResult("No variables (is the session stopped?)."));
                        return;
                    }
                    Json results = Json::array();
                    for (const dap::DapManager::Variable& variable : variables) {
                        Json entry = Json{{"name", variable.name}, {"value", variable.value}};
                        if (!variable.type.empty()) {
                            entry["type"] = variable.type;
                        }
                        if (variable.variablesReference > 0) {
                            entry["variablesReference"] = variable.variablesReference;
                        }
                        results.push_back(std::move(entry));
                    }
                    callback(MakeTextToolResult(results.dump()));
                },
                hex);
        });

    RegisterTool(
        "dap_evaluate", "Evaluate an expression in the stopped debug session's top (or focused) frame.",
        Json{
            {"type", "object"},
            {"properties", {{"expression", {{"type", "string"}, {"description", "The expression to evaluate, in the adapter's own expression syntax."}}}}},
            {"required", Json::array({"expression"})},
        },
        [this](const Json& args, const ResultCallback& callback) {
            const auto expression = RequireString(args, "expression");
            if (!expression) {
                callback(MakeTextToolResult("Missing required argument: expression", true));
                return;
            }
            dapManager_.Evaluate(*expression,
                                 [callback](bool success, std::string text) { callback(MakeTextToolResult(std::move(text), !success)); });
        });

    RegisterTool(
        "dap_list_watches", "List every watch expression, with its recent numeric history where available.", Json{{"type", "object"}, {"properties", Json::object()}},
        [this](const Json&, const ResultCallback& callback) {
            const std::vector<std::string>& watches = dapManager_.Watches();
            if (watches.empty()) {
                callback(MakeTextToolResult("No watches set."));
                return;
            }
            Json results = Json::array();
            for (std::size_t i = 0; i < watches.size(); ++i) {
                Json entry = Json{{"expression", watches[i]}};
                if (const std::vector<double>& history = dapManager_.WatchHistoryAt(i); !history.empty()) {
                    entry["history"] = history;
                }
                results.push_back(std::move(entry));
            }
            callback(MakeTextToolResult(results.dump()));
        });
}

} // namespace ned::editor::mcp
