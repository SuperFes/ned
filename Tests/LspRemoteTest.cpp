#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Lsp/Manager.h"
#include "Editor/Lsp/RootResolver.h"
#include "Editor/Lsp/ServerConfig.h"
#include "Editor/Remote/Hosts.h"
#include "FakeRemoteHost.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/FileUri.h"
#include "Text/Rope.h"
#include "UI/EventLoop.h"

namespace fs     = std::filesystem;
namespace lsp    = ned::editor::lsp;
namespace remote = ned::editor::remote;
namespace text   = ned::text;
using Json       = nlohmann::json;
using ned::tests::PumpUntil;

namespace {

constexpr const char* kLanguage = "remote-lsp-test";

// Logs its working directory, then every message it is sent, one JSON
// object a line. Answers initialize and hover, and reports a diagnostic on
// each document opened.
constexpr const char* kServer = R"(import json, os, sys
log = open(sys.argv[1], "a")
log.write(json.dumps({"cwd": os.getcwd()}) + "\n")
log.flush()
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
    elif method == "textDocument/didOpen":
        uri = message["params"]["textDocument"]["uri"]
        reply = {"method": "textDocument/publishDiagnostics", "params": {"uri": uri, "diagnostics": [
            {"range": {"start": {"line": 0, "character": 4}, "end": {"line": 0, "character": 5}}, "severity": 1, "message": "seen on the host"}]}}
    if reply:
        body = json.dumps(dict(jsonrpc="2.0", **reply)).encode()
        out.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        out.flush()
)";

struct Fixture {
    ned::tests::FakeRemoteHost fake;
    ned::ui::EventLoop         loop;
    remote::Hosts              hosts{loop, [this](const std::string& destination) { return fake.Options(destination); }, ned::tests::FakeResolver(fake.dir)};
    fs::path                   log = fake.dir / "server.log";
    fs::path                   pkg;
    fs::path                   file;

    Fixture() {
        const fs::path project = fake.dir / "project";
        fs::create_directories(project / ".git");
        fs::create_directories(project / "pkg" / "src");
        std::ofstream(project / "pkg" / "pkg.marker");
        std::ofstream(fake.dir / "server.py") << kServer;
        pkg  = fs::canonical(project / "pkg");
        file = pkg / "src" / "a.c";
        lsp::SetLspServerCommand(kLanguage, {"python3", (fake.dir / "server.py").string(), log.string()});
        lsp::SetLspRootMarkers(kLanguage, {"pkg.marker"});
    }
    ~Fixture() {
        lsp::SetLspServerCommand(kLanguage, {});
        lsp::SetLspRootMarkers(kLanguage, {});
    }
    Fixture(const Fixture&)            = delete;
    Fixture& operator=(const Fixture&) = delete;

    [[nodiscard]] text::Buffer& Open(text::BufferList& bufferList) const {
        text::Buffer& buffer = bufferList.AddRemotePlaceholder({.destination = "host", .path = file});
        buffer.FinishLoad(text::Rope("int x;\n"));
        return buffer;
    }
    bool Connect() {
        bool connected = false;
        hosts.Connect("host", [&](std::expected<remote::Connection*, std::string> connection) { connected = connection.has_value(); });
        return PumpUntil(loop, [&] { return connected; });
    }

    [[nodiscard]] std::vector<Json> Messages() const {
        std::vector<Json> messages;
        std::ifstream     in(log);
        for (std::string line; std::getline(in, line);) {
            messages.push_back(Json::parse(line));
        }
        return messages;
    }
    [[nodiscard]] std::optional<Json> Find(const std::string& method) const {
        for (Json& message : Messages()) {
            if (message.value("method", "") == method) {
                return std::move(message);
            }
        }
        return std::nullopt;
    }
};

} // namespace

TEST_CASE("A remote buffer's language server runs on its host, in its language's root there", "[Lsp][LspRemote][HostAgent]") {
    Fixture f;
    REQUIRE(f.Connect());

    text::BufferList bufferList;
    lsp::Manager     manager(bufferList, f.loop);
    manager.SetRemoteHosts(&f.hosts);
    text::Buffer& buffer = f.Open(bufferList);

    REQUIRE(PumpUntil(f.loop, [&] {
        manager.SyncBuffer(buffer, kLanguage);
        return f.Find("textDocument/didOpen").has_value();
    }));

    const std::vector<Json> messages = f.Messages();
    REQUIRE(!messages.empty());
    CHECK(messages.front()["cwd"] == f.pkg.string());
    const Json initialize = *f.Find("initialize");
    CHECK(initialize["params"]["rootUri"] == text::PathToFileUri(f.pkg));
    CHECK(initialize["params"]["processId"].is_null());
    const Json didOpen = *f.Find("textDocument/didOpen");
    CHECK(didOpen["params"]["textDocument"]["uri"] == text::PathToFileUri(f.file));
    CHECK(didOpen["params"]["textDocument"]["text"] == "int x;\n");
}

TEST_CASE("A remote buffer's language server waits for its host; nothing connects to start one", "[Lsp][LspRemote][HostAgent]") {
    Fixture f;

    text::BufferList bufferList;
    lsp::Manager     manager(bufferList, f.loop);
    manager.SetRemoteHosts(&f.hosts);
    text::Buffer& buffer = f.Open(bufferList);

    CHECK_FALSE(PumpUntil(
        f.loop,
        [&] {
            manager.SyncBuffer(buffer, kLanguage);
            return f.hosts.Find("host") != nullptr || f.hosts.Connecting("host");
        },
        std::chrono::milliseconds(300)));
    CHECK_FALSE(fs::exists(f.log));
}

TEST_CASE("A remote server's diagnostics and answers reach the buffer it means", "[Lsp][LspRemote][HostAgent]") {
    Fixture f;
    REQUIRE(f.Connect());

    text::BufferList bufferList;
    lsp::Manager     manager(bufferList, f.loop);
    manager.SetRemoteHosts(&f.hosts);
    text::Buffer& buffer = f.Open(bufferList);

    REQUIRE(PumpUntil(f.loop, [&] {
        manager.SyncBuffer(buffer, kLanguage);
        return !buffer.Diagnostics().empty();
    }));
    REQUIRE(buffer.Diagnostics().size() == 1);
    CHECK(buffer.Diagnostics().front().message == "seen on the host");
    CHECK(buffer.Diagnostics().front().startByte == 4);
    CHECK(manager.ProjectDiagnostics().empty());

    std::optional<std::optional<std::string>> hover;
    manager.RequestHover(buffer, 4, [&](std::optional<std::string> text) { hover = std::move(text); });
    REQUIRE(PumpUntil(f.loop, [&] { return hover.has_value(); }));
    REQUIRE(hover->has_value());
    CHECK(hover->value().starts_with("hover from "));
}
