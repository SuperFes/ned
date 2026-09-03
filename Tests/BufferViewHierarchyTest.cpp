#include <catch2/catch_test_macros.hpp>

#include <poll.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Lsp/LspClient.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Lsp/Transport.h"
#include "Editor/Mode.h"
#include "Editor/ProjectRoot.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EventLoop.h"
#include "UI/Theme.h"
#include "UI/TreeView.h"
#include "UI/Widget.h"

using ned::text::Buffer;
using ned::ui::BufferView;

namespace {

// Mirrors BufferViewProjectFindReferencesTest.cpp's own Fixture exactly.
struct Fixture {
    ned::text::Buffer          buffer{"scratch"};
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::editor::PromptHistory promptHistory;
    ned::text::BufferList      bufferList;

    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

// Mirrors BufferViewProjectFindReferencesTest.cpp's own FakeLspServer/
// ReadRawLspFrame/DrainAllPendingFrames exactly -- kept file-local here too,
// same "not worth a new cross-translation-unit dependency" precedent.
struct FakeLspServer {
    int serverStdinRead;
    int serverStdoutWrite;

    FakeLspServer(int readFd, int writeFd) : serverStdinRead(readFd), serverStdoutWrite(writeFd) {
    }
    ~FakeLspServer() {
        ::close(serverStdoutWrite);
        ::close(serverStdinRead);
    }
    FakeLspServer(const FakeLspServer&)            = delete;
    FakeLspServer& operator=(const FakeLspServer&) = delete;
    FakeLspServer(FakeLspServer&&)                 = default;

    static FakeLspServer Create(ned::editor::lsp::LspManager& manager, const std::string& language, ned::ui::EventLoop& eventLoop,
                                ned::editor::lsp::LspClient*& outClient) {
        int clientWritesHere[2];
        int clientReadsHere[2];
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        auto client = std::make_unique<ned::editor::lsp::LspClient>(ned::editor::lsp::Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop);
        outClient   = &manager.SetClientForTesting(language, std::move(client));
        return FakeLspServer(clientWritesHere[0], clientReadsHere[1]);
    }
};

std::string ReadRawLspFrame(int fd) {
    std::string all;
    char        buffer[512];
    for (int i = 0; i < 4; ++i) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        all.append(buffer, static_cast<std::size_t>(n));
        const auto headerEnd = all.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            const std::string_view kPrefix   = "Content-Length: ";
            const auto             prefixPos = all.find(kPrefix);
            if (prefixPos != std::string::npos) {
                const std::size_t contentLength = std::stoul(all.substr(prefixPos + kPrefix.size()));
                if (all.size() >= headerEnd + 4 + contentLength) {
                    break;
                }
            }
        }
    }
    return all;
}

int LspRequestIdFromFrame(const std::string& raw) {
    return ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["id"].get<int>();
}

bool NoFrameArrives(int fd) {
    pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
    return ::poll(&pfd, 1, 200) == 0;
}

void DrainAllPendingFrames(int fd) {
    std::string all;
    char        buffer[512];
    while (!NoFrameArrives(fd)) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        all.append(buffer, static_cast<std::size_t>(n));
    }
}

} // namespace

TEST_CASE("RequestHierarchyAtPoint(IncomingCalls) sends prepareCallHierarchy, then auto-expands the root via "
          "incomingCalls, pushing a two-row model",
          "[BufferView][Hierarchy][Lsp]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_hierarchy_test.cpp";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("callee();\n");
    buffer.SetPoint(0);
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    ned::ui::Screen screenBuf(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // syncs the buffer -- see SyncBuffer's own didOpen
    DrainAllPendingFrames(server.serverStdinRead);

    std::vector<std::optional<ned::ui::TreeViewModel>> pushedModels;
    view.SetOnHierarchyChanged([&](std::optional<ned::ui::TreeViewModel> model) { pushedModels.push_back(std::move(model)); });

    view.RequestHierarchyAtPointForTesting(BufferView::HierarchyDirection::IncomingCalls);

    const std::string prepareRaw = ReadRawLspFrame(server.serverStdinRead);
    const auto        prepareReq = ned::editor::lsp::Json::parse(prepareRaw.substr(prepareRaw.find("\r\n\r\n") + 4));
    REQUIRE(prepareReq["method"] == "textDocument/prepareCallHierarchy");

    const std::string ownUri = "file://" + path.string();
    const auto        rootItem = ned::editor::lsp::Json{
        {"name", "callee"},
        {"kind", 12},
        {"uri", ownUri},
        {"selectionRange", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 6}}}}},
        {"data", {{"opaque", 1}}},
    };
    client->DispatchFrame(ned::editor::lsp::Json{
        {"jsonrpc", "2.0"}, {"id", LspRequestIdFromFrame(prepareRaw)}, {"result", ned::editor::lsp::Json::array({rootItem})}}
                              .dump());

    // The root's own auto-expand fires immediately after prepare resolves,
    // as callHierarchy/incomingCalls -- with the *same* item verbatim.
    const std::string incomingRaw = ReadRawLspFrame(server.serverStdinRead);
    const auto        incomingReq = ned::editor::lsp::Json::parse(incomingRaw.substr(incomingRaw.find("\r\n\r\n") + 4));
    REQUIRE(incomingReq["method"] == "callHierarchy/incomingCalls");
    REQUIRE(incomingReq["params"]["item"] == rootItem);

    // A "loading" model should already have been pushed for the root by
    // this point (BeginLoading fires before the request is even sent).
    REQUIRE_FALSE(pushedModels.empty());
    REQUIRE(pushedModels.back().has_value());
    REQUIRE(pushedModels.back()->rows.size() == 1);
    REQUIRE(pushedModels.back()->rows[0].loading);

    const std::filesystem::path callerPath = std::filesystem::temp_directory_path() / "ned_hierarchy_caller_test.cpp";
    const auto                  callerItem = ned::editor::lsp::Json{
        {"name", "caller"},
        {"kind", 12},
        {"uri", "file://" + callerPath.string()},
        {"selectionRange", {{"start", {{"line", 3}, {"character", 0}}}, {"end", {{"line", 3}, {"character", 6}}}}},
    };
    client->DispatchFrame(ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(incomingRaw)},
        {"result", ned::editor::lsp::Json::array({{{"from", callerItem}, {"fromRanges", ned::editor::lsp::Json::array()}}})},
    }
                              .dump());

    REQUIRE(pushedModels.back().has_value());
    const ned::ui::TreeViewModel& finalModel = *pushedModels.back();
    REQUIRE(finalModel.title == "Callers of callee");
    REQUIRE(finalModel.rows.size() == 2);
    REQUIRE(finalModel.rows[0].depth == 0);
    REQUIRE(finalModel.rows[0].expanded);
    REQUIRE_FALSE(finalModel.rows[0].loading);
    REQUIRE(finalModel.rows[1].depth == 1);
    REQUIRE(finalModel.rows[1].label.find("caller") != std::string::npos);
    // "caller" itself has never been expanded (only the root was
    // auto-expanded) -- ChildrenFetched is false for it, so hasChildren
    // defaults to true ("not yet asked, assume it might have some").
    REQUIRE(finalModel.rows[1].hasChildren);
}

TEST_CASE("HierarchyActivate jumps to the selected row's location and ends the session (nullopt model, no lingering "
          "state)",
          "[BufferView][Hierarchy][Lsp]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_hierarchy_activate_test.cpp";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("callee();\n");
    buffer.SetPoint(0);
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    ned::ui::Screen screenBuf(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    DrainAllPendingFrames(server.serverStdinRead);

    bool sawNullopt = false;
    view.SetOnHierarchyChanged([&](std::optional<ned::ui::TreeViewModel> model) {
        if (!model) {
            sawNullopt = true;
        }
    });

    view.RequestHierarchyAtPointForTesting(BufferView::HierarchyDirection::OutgoingCalls);
    const std::string prepareRaw = ReadRawLspFrame(server.serverStdinRead);
    const std::string ownUri     = "file://" + path.string();
    const auto        rootItem   = ned::editor::lsp::Json{
        {"name", "callee"},
        {"kind", 12},
        {"uri", ownUri},
        {"selectionRange", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 6}}}}},
    };
    client->DispatchFrame(ned::editor::lsp::Json{
        {"jsonrpc", "2.0"}, {"id", LspRequestIdFromFrame(prepareRaw)}, {"result", ned::editor::lsp::Json::array({rootItem})}}
                              .dump());

    const std::string outgoingRaw = ReadRawLspFrame(server.serverStdinRead);
    REQUIRE(ned::editor::lsp::Json::parse(outgoingRaw.substr(outgoingRaw.find("\r\n\r\n") + 4))["method"] == "callHierarchy/outgoingCalls");
    client->DispatchFrame(
        ned::editor::lsp::Json{{"jsonrpc", "2.0"}, {"id", LspRequestIdFromFrame(outgoingRaw)}, {"result", ned::editor::lsp::Json::array()}}.dump());

    REQUIRE_FALSE(sawNullopt); // session still open -- root expanded with zero children, not activated yet

    view.HierarchyActivate(0); // jump to the root itself
    REQUIRE(sawNullopt);       // EndHierarchySession fired onHierarchyChanged_(nullopt)
    REQUIRE(&fixture.activeBuffer.Get() == &buffer);
    REQUIRE(buffer.Point() == 0); // selectionRange.start {0,0}
}
