#include "McpToolRegistry.h"

#include <algorithm>
#include <system_error>

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

ToolRegistry::ToolRegistry(text::BufferList& bufferList, lsp::LspManager& lspManager, vcs::VcsRunner& vcsRunner, testrun::TestRunner& testRunner)
    : bufferList_(bufferList), lspManager_(lspManager), vcsRunner_(vcsRunner), testRunner_(testRunner) {
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
}

} // namespace ned::editor::mcp
