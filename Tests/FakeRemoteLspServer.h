//
// A language server for tests on a fake host (FakeRemoteHost.h): a Python
// script the host runs, logging what it is sent, over a small project with
// its own root marker.
//

#ifndef NED_TESTS_FAKEREMOTELSPSERVER_H
#define NED_TESTS_FAKEREMOTELSPSERVER_H

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Lsp/RootResolver.h"
#include "Editor/Lsp/ServerConfig.h"

namespace ned::tests {

inline constexpr const char* kRemoteLspLanguage = "remote-lsp-test";

// Logs its working directory, then every message it is sent, one JSON
// object a line. Reports a diagnostic on each document opened, and answers
// initialize, hover, definition (b.c, line 1, column 4), references
// (there and a.c, line 0, column 4), a workspace symbol y (at the
// definition) and switchSourceHeader (b.c).
inline constexpr const char* kRemoteLspServer = R"(import json, os, sys
log = open(sys.argv[1], "a")
log.write(json.dumps({"cwd": os.getcwd()}) + "\n")
log.flush()
here = "file://" + os.getcwd() + "/src/"
def at(name, line):
    return {"uri": here + name, "range": {"start": {"line": line, "character": 4}, "end": {"line": line, "character": 5}}}
inp, out = sys.stdin.buffer, sys.stdout.buffer
while True:
    length = None
    while True:
        line = inp.readline()
        if not line:
            sys.exit(0)
        line = line.strip()
        if not line:
            break
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":")[1])
    message = json.loads(inp.read(length))
    log.write(json.dumps(message) + "\n")
    log.flush()
    method, reply = message.get("method"), None
    if method == "initialize":
        reply = {"id": message["id"], "result": {"capabilities": {"textDocumentSync": 1, "hoverProvider": True}}}
    elif method == "textDocument/hover":
        reply = {"id": message["id"], "result": {"contents": "hover from " + os.uname().nodename}}
    elif method == "textDocument/definition":
        reply = {"id": message["id"], "result": [at("b.c", 1)]}
    elif method == "textDocument/references":
        reply = {"id": message["id"], "result": [at("a.c", 0), at("b.c", 1)]}
    elif method == "workspace/symbol":
        reply = {"id": message["id"], "result": [{"name": "y", "kind": 13, "location": at("b.c", 1)}]}
    elif method == "textDocument/switchSourceHeader":
        reply = {"id": message["id"], "result": here + "b.c"}
    elif method == "textDocument/didOpen":
        uri = message["params"]["textDocument"]["uri"]
        reply = {"method": "textDocument/publishDiagnostics", "params": {"uri": uri, "diagnostics": [
            {"range": {"start": {"line": 0, "character": 4}, "end": {"line": 0, "character": 5}}, "severity": 1, "message": "seen on the host"}]}}
    if reply:
        body = json.dumps(dict(jsonrpc="2.0", **reply)).encode()
        out.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        out.flush()
)";

// dir/project/pkg is the language's root, holding src/a.c ("int x;\n") and
// src/b.c ("// b\nint y;\n"), inside a repository at dir/project.
struct FakeRemoteLspServer {
    std::filesystem::path log;
    std::filesystem::path pkg;
    std::filesystem::path file;
    std::filesystem::path other;

    explicit FakeRemoteLspServer(const std::filesystem::path& dir) : log(dir / "server.log") {
        const std::filesystem::path project = dir / "project";
        std::filesystem::create_directories(project / ".git");
        std::filesystem::create_directories(project / "pkg" / "src");
        std::ofstream(project / "pkg" / "pkg.marker");
        std::ofstream(project / "pkg" / "src" / "a.c") << "int x;\n";
        std::ofstream(project / "pkg" / "src" / "b.c") << "// b\nint y;\n";
        std::ofstream(dir / "server.py") << kRemoteLspServer;
        pkg   = std::filesystem::canonical(project / "pkg");
        file  = pkg / "src" / "a.c";
        other = pkg / "src" / "b.c";
        editor::lsp::SetLspServerCommand(kRemoteLspLanguage, {"python3", (dir / "server.py").string(), log.string()});
        editor::lsp::SetLspRootMarkers(kRemoteLspLanguage, {"pkg.marker"});
    }
    ~FakeRemoteLspServer() {
        editor::lsp::SetLspServerCommand(kRemoteLspLanguage, {});
        editor::lsp::SetLspRootMarkers(kRemoteLspLanguage, {});
    }
    FakeRemoteLspServer(const FakeRemoteLspServer&)            = delete;
    FakeRemoteLspServer& operator=(const FakeRemoteLspServer&) = delete;

    [[nodiscard]] std::vector<nlohmann::json> Messages() const {
        std::vector<nlohmann::json> messages;
        std::ifstream               in(log);
        for (std::string line; std::getline(in, line);) {
            messages.push_back(nlohmann::json::parse(line));
        }
        return messages;
    }
    [[nodiscard]] std::optional<nlohmann::json> Find(const std::string& method) const {
        for (nlohmann::json& message : Messages()) {
            if (message.value("method", "") == method) {
                return std::move(message);
            }
        }
        return std::nullopt;
    }
};

} // namespace ned::tests

#endif // NED_TESTS_FAKEREMOTELSPSERVER_H
