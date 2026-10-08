#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Lsp/Manager.h"
#include "Editor/Remote/Hosts.h"
#include "FakeRemoteHost.h"
#include "FakeRemoteLspServer.h"
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

constexpr const char* kLanguage = ned::tests::kRemoteLspLanguage;

struct Fixture {
    ned::tests::FakeRemoteHost      fake;
    ned::ui::EventLoop              loop;
    remote::Hosts                   hosts{loop, [this](const std::string& destination) { return fake.Options(destination); }, ned::tests::FakeResolver(fake.dir)};
    ned::tests::FakeRemoteLspServer server{fake.dir};

    [[nodiscard]] text::Buffer& Open(text::BufferList& bufferList) const {
        text::Buffer& buffer = bufferList.AddRemotePlaceholder({.destination = "host", .path = server.file});
        buffer.FinishLoad(text::Rope("int x;\n"));
        return buffer;
    }
    bool Connect() {
        bool connected = false;
        hosts.Connect("host", [&](std::expected<remote::Connection*, std::string> connection) { connected = connection.has_value(); });
        return PumpUntil(loop, [&] { return connected; });
    }
    // Syncs buffer until its server has it open.
    bool Opened(lsp::Manager& manager, text::Buffer& buffer) {
        return PumpUntil(loop, [&] {
            manager.SyncBuffer(buffer, kLanguage);
            return server.Find("textDocument/didOpen").has_value();
        });
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

    REQUIRE(f.Opened(manager, buffer));

    const std::vector<Json> messages = f.server.Messages();
    REQUIRE(!messages.empty());
    CHECK(messages.front()["cwd"] == f.server.pkg.string());
    const Json initialize = *f.server.Find("initialize");
    CHECK(initialize["params"]["rootUri"] == text::PathToFileUri(f.server.pkg));
    CHECK(initialize["params"]["processId"].is_null());
    const Json didOpen = *f.server.Find("textDocument/didOpen");
    CHECK(didOpen["params"]["textDocument"]["uri"] == text::PathToFileUri(f.server.file));
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
    CHECK_FALSE(fs::exists(f.server.log));
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

TEST_CASE("A remote server's locations name files on its host", "[Lsp][LspRemote][HostAgent]") {
    Fixture f;
    REQUIRE(f.Connect());

    text::BufferList bufferList;
    lsp::Manager     manager(bufferList, f.loop);
    manager.SetRemoteHosts(&f.hosts);
    text::Buffer& buffer = f.Open(bufferList);
    REQUIRE(f.Opened(manager, buffer));

    std::optional<std::vector<lsp::Manager::ResolvedLocation>> locations;
    manager.RequestDefinition(buffer, 4, [&](std::vector<lsp::Manager::ResolvedLocation> found) { locations = std::move(found); });
    REQUIRE(PumpUntil(f.loop, [&] { return locations.has_value(); }));
    REQUIRE(locations->size() == 1);
    CHECK(locations->front().file == text::FileRef(text::RemoteFile{.destination = "host", .path = f.server.other}));
    CHECK(locations->front().position.line == 1);
    CHECK(locations->front().position.character == 4);
}

TEST_CASE("A remote server's symbols and counterparts name files on its host", "[Lsp][LspRemote][HostAgent]") {
    Fixture f;
    REQUIRE(f.Connect());

    text::BufferList bufferList;
    lsp::Manager     manager(bufferList, f.loop);
    manager.SetRemoteHosts(&f.hosts);
    text::Buffer& buffer = f.Open(bufferList);
    REQUIRE(f.Opened(manager, buffer));
    const text::FileRef other = text::RemoteFile{.destination = "host", .path = f.server.other};

    std::optional<std::vector<lsp::Manager::SymbolResult>> symbols;
    manager.RequestWorkspaceSymbols(buffer, "y", [&](std::vector<lsp::Manager::SymbolResult> found) { symbols = std::move(found); });
    REQUIRE(PumpUntil(f.loop, [&] { return symbols.has_value(); }));
    REQUIRE(symbols->size() == 1);
    CHECK(symbols->front().file == other);

    std::optional<std::optional<text::FileRef>> counterpart;
    manager.RequestSwitchSourceHeader(buffer, [&](std::optional<text::FileRef> found) { counterpart = std::move(found); });
    REQUIRE(PumpUntil(f.loop, [&] { return counterpart.has_value(); }));
    CHECK(*counterpart == std::optional(other));
}
