//
// DAP client — slice 1. Per-language debug-adapter configuration, mirroring
// Lsp/ServerConfig.h's exact "mutex-guarded static maps, empty clears"
// pattern: nothing is bundled or auto-detected, the user configures each
// adapter from init.janet (ned/set-dap-adapter, ned/set-dap-launch) the same
// way LSP servers are — see ROADMAP.md's DAP entry for the full design
// record.
//
// Two independent settings per language:
//  - the adapter command (argv[0] + args, e.g. {"lldb-dap"} or
//    {"python", "-m", "debugpy.adapter"}) — the subprocess Manager spawns;
//  - the launch configuration (a JSON object as text, e.g.
//    R"({"program": "./build/ned"})") — passed verbatim as the DAP `launch`
//    request's own `arguments`. Adapter-specific by design in the protocol
//    itself (debugpy and lldb-dap want different keys), so this deliberately
//    stays an opaque JSON string rather than a curated struct that would
//    trail every adapter's own schema.
//

#ifndef NED_EDITOR_DAP_CONFIG_H
#define NED_EDITOR_DAP_CONFIG_H

#include <optional>
#include <string>
#include <vector>

namespace ned::editor::dap {

// An empty argv clears any existing registration for language, mirroring
// SetLspServerCommand's own empty-clears convention.
void                                                  SetAdapterCommand(const std::string& language, std::vector<std::string> argv);
[[nodiscard]] std::optional<std::vector<std::string>> AdapterCommand(const std::string& language);

// launchConfigJson is not validated here — it's parsed (and any parse error
// surfaced) by Manager at launch time, the moment a real error message
// has somewhere useful to go. An empty string clears.
void                                     SetLaunchConfig(const std::string& language, std::string launchConfigJson);
[[nodiscard]] std::optional<std::string> LaunchConfig(const std::string& language);

// DAP round 3: the `attach` request's own arguments object -- same opaque,
// unvalidated, adapter-specific JSON-string shape as the launch config
// above, just for Manager::Attach instead of StartOrContinue's launch
// path. An empty string clears.
void                                     SetAttachConfig(const std::string& language, std::string attachConfigJson);
[[nodiscard]] std::optional<std::string> AttachConfig(const std::string& language);

} // namespace ned::editor::dap

#endif // NED_EDITOR_DAP_CONFIG_H
