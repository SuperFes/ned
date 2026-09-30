//
// ACP MCP tool-server bridge, slice 1. The name -> {JSON schema, handler}
// table backing tools/list and tools/call -- see ROADMAP.md's "ACP MCP
// tool-server bridge" entry for why this exists at all (today the connected
// agent's only structured capability is fs/read_text_file/write_text_file;
// this exposes a first, deliberately read-only slice of ned's own
// already-correct subsystems -- LSP, VCS, project search, the test runner --
// as real MCP tools instead of the agent having to shell out blind).
//
// Not a fully pure router like Lsp/Broker.h's BrokerRouter -- a tool
// handler genuinely calls into a live async manager (Manager/Runner)
// rather than just describing an action to carry out elsewhere -- but the
// name/schema/dispatch bookkeeping here is unit-testable the same way with
// stub handlers. Every dependency is injected by reference at construction
// (Manager&/Runner&/TestRunner&/Manager&/text::BufferList&),
// matching this codebase's "Set*/register-then-connect" cross-manager wiring
// convention rather than a god-object reaching for process-wide statics.
//
// Every manager call here already resolves/responds on the main thread
// (Manager/Runner/TestRunner all marshal their own async I/O onto the
// main thread via EventLoop::Post before invoking a caller's callback -- see
// each header's own doc comments) -- so CallTool's own `callback` always
// fires on the main thread too, synchronously for a handler with no
// out-of-process work (get_diagnostics, search_project, run_tests,
// get_test_results) or later for one that genuinely round-trips to a
// language server/git subprocess (hover, goto_definition, find_references,
// git_status, git_diff). No per-call generation counter is needed: each
// CallTool invocation gets its own fresh `callback` closure, never a shared
// slot another call could overwrite.
//

#ifndef NED_EDITOR_MCP_TOOLREGISTRY_H
#define NED_EDITOR_MCP_TOOLREGISTRY_H

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Lsp/Manager.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor::vcs {
class Runner;
} // namespace ned::editor::vcs

namespace ned::editor::testrun {
class TestRunner;
} // namespace ned::editor::testrun

namespace ned::editor::dap {
class Manager;
} // namespace ned::editor::dap

namespace ned::editor::mcp {

using Json = nlohmann::json;

// One entry's worth of tools/list metadata -- name/description/inputSchema
// map directly onto MCP's own Tool object (see the spec's "server/tools"
// page); inputSchema is a plain JSON Schema object.
struct ToolDescriptor {
    std::string name;
    std::string description;
    Json        inputSchema;
};

// Builds a well-formed MCP tools/call result envelope
// ({"content": [{"type": "text", "text": text}], "isError": isError}) --
// every handler below funnels its outcome through this rather than
// hand-building the envelope inline.
[[nodiscard]] Json MakeTextToolResult(std::string text, bool isError = false);

// What a tool needs from the window layer, which this registry can't reach:
// a workspace edit applies through a pane (resource ops, project undo), and
// navigation moves one. Set after the WindowManager exists; a tool whose
// hook is unset reports itself unavailable.
struct EditorHooks {
    // Applies `edit` as one undoable step, labelled `label`; false if
    // nothing was applied.
    std::function<bool(const lsp::Manager::ResolvedRename& edit, const std::string& label)> applyWorkspaceEdit;
    // Shows `path` at `line` (1-indexed); false if the editor declined to
    // move, e.g. because the user is typing in it.
    std::function<bool(const std::filesystem::path& path, std::size_t line)> visitLocation;
};

class ToolRegistry {
  public:
    ToolRegistry(text::BufferList& bufferList, lsp::Manager& lspManager, vcs::Runner& vcsRunner, testrun::TestRunner& testRunner,
                 dap::Manager& dapManager);

    [[nodiscard]] std::vector<ToolDescriptor> ListTools() const;

    using ResultCallback = std::function<void(Json toolResult)>;

    // Looks up `name`; if found, invokes its handler with `arguments`, which
    // eventually calls `callback` exactly once with a fully-shaped
    // tools/call result (see MakeTextToolResult above) -- a tool-level
    // failure (bad git state, no LSP server for the buffer's language, a
    // malformed pattern, ...) is reported via isError:true in that result,
    // matching MCP's own "Tool Execution Errors" convention, not a
    // JSON-RPC-level error. If `name` isn't registered, calls `callback`
    // synchronously with isError:true -- a caller needing a genuine
    // JSON-RPC "Unknown tool" protocol error instead (MCP's other error
    // reporting mechanism) checks HasTool first.
    void CallTool(const std::string& name, const Json& arguments, ResultCallback callback) const;

    [[nodiscard]] bool HasTool(const std::string& name) const;

    void SetEditorHooks(EditorHooks hooks);

  private:
    using Handler = std::function<void(const Json& arguments, const ResultCallback& callback)>;

    struct Entry {
        ToolDescriptor descriptor;
        Handler        handler;
    };

    void RegisterTool(std::string name, std::string description, Json inputSchema, Handler handler);
    void RegisterBuiltinTools();

    // Applies `edit` through the hook, then saves each touched buffer that
    // had no unsaved changes beforehand -- an agent expects its edits on
    // disk, but a user's in-progress edits are never saved behind them.
    // Returns a summary object, or {"error": ...} when nothing was applied.
    [[nodiscard]] Json ApplyAndSave(const lsp::Manager::ResolvedRename& edit, const std::string& label) const;

    // The tail of apply_code_action once the action is final: its edit
    // first, then its command, spec order. `buffer` is the one the action
    // was requested against, re-confirmed open via `file` before use.
    void ApplyCodeAction(const std::string& file, const text::Buffer* buffer, const lsp::CodeAction& action,
                         const ResultCallback& callback) const;

    text::BufferList&    bufferList_;
    lsp::Manager&     lspManager_;
    vcs::Runner&      vcsRunner_;
    testrun::TestRunner& testRunner_;
    dap::Manager&     dapManager_;
    EditorHooks          editorHooks_;
    std::vector<Entry>   entries_;
};

} // namespace ned::editor::mcp

#endif // NED_EDITOR_MCP_TOOLREGISTRY_H
