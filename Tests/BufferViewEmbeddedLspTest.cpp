#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>

#include <unistd.h>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Lsp/LspClient.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Lsp/Transport.h"
#include "Editor/Mode.h"
#include "Editor/Multibuffer.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EventLoop.h"
#include "UI/Theme.h"

using ned::editor::multibuffer::ClearRegistryForTesting;
using ned::text::Buffer;
using ned::ui::BufferView;

namespace {

// Mirrors BufferViewDiagnosticsBufferTest.cpp's own RegistryResetGuard.
struct RegistryResetGuard {
    RegistryResetGuard() {
        ClearRegistryForTesting();
    }
    ~RegistryResetGuard() {
        ClearRegistryForTesting();
    }
};

// Mirrors BufferViewDiagnosticsBufferTest.cpp's own Fixture, except mode
// defaults to HtmlMode -- every test in this file needs a real
// mode.embeddedRegions hook.
struct Fixture {
    RegistryResetGuard         registryResetGuard;
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
    ned::editor::Mode            mode  = ned::editor::HtmlMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

// Mirrors BufferViewTest.cpp's own FakeLspServer/ReadRawLspFrame exactly
// (kept file-local here too, matching that file's own "not worth a new
// dependency between two test binaries' translation units" precedent).
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

} // namespace

TEST_CASE("BufferView::EmbeddedLanguageAtPoint tracks point moving in and out of an embedded <script> region",
          "[BufferView][EmbeddedDocuments]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("<div></div><script>let x = 1;</script>");
    BufferView view = fixture.View();

    fixture.buffer.SetPoint(2); // inside <div>
    REQUIRE_FALSE(view.EmbeddedLanguageAtPoint().has_value());

    const std::size_t scriptContentOffset = fixture.buffer.Text().find("let x");
    REQUIRE(scriptContentOffset != std::string::npos);
    fixture.buffer.SetPoint(scriptContentOffset);
    REQUIRE(view.EmbeddedLanguageAtPoint() == std::optional<std::string>("javascript"));

    fixture.buffer.SetPoint(2);
    REQUIRE_FALSE(view.EmbeddedLanguageAtPoint().has_value());
}

TEST_CASE("BufferView::EmbeddedLanguageAtPoint is always nullopt for a mode with no embeddedRegions hook",
          "[BufferView][EmbeddedDocuments]") {
    Fixture fixture;
    fixture.mode = ned::editor::FundamentalMode();
    fixture.buffer.InsertAtPoint("anything");
    BufferView view = fixture.View();

    REQUIRE_FALSE(view.EmbeddedLanguageAtPoint().has_value());
}

TEST_CASE("Painting an HTML buffer with a <script> block spawns and syncs the javascript server", "[BufferView][EmbeddedDocuments]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_embedded_lsp_open_test.html";
    Buffer&                     buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* htmlClient = nullptr;
    ned::editor::lsp::LspClient* jsClient   = nullptr;
    FakeLspServer                htmlServer = FakeLspServer::Create(manager, "html", eventLoop, htmlClient);
    FakeLspServer                jsServer   = FakeLspServer::Create(manager, "javascript", eventLoop, jsClient);

    BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // triggers SyncBuffer -> html didOpen, SyncEmbeddedDocuments -> javascript didOpen

    const std::string htmlRaw = ReadRawLspFrame(htmlServer.serverStdinRead);
    REQUIRE(htmlRaw.find("textDocument/didOpen") != std::string::npos);

    const std::string jsRaw = ReadRawLspFrame(jsServer.serverStdinRead);
    REQUIRE(jsRaw.find("textDocument/didOpen") != std::string::npos);
    REQUIRE(jsRaw.find("\"languageId\":\"javascript\"") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("Deleting the only <script> block and repainting tears down the javascript server", "[BufferView][EmbeddedDocuments]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_embedded_lsp_teardown_test.html";
    Buffer&                     buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* htmlClient = nullptr;
    ned::editor::lsp::LspClient* jsClient   = nullptr;
    FakeLspServer                htmlServer = FakeLspServer::Create(manager, "html", eventLoop, htmlClient);
    FakeLspServer                jsServer   = FakeLspServer::Create(manager, "javascript", eventLoop, jsClient);

    BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    (void)ReadRawLspFrame(htmlServer.serverStdinRead);
    (void)ReadRawLspFrame(jsServer.serverStdinRead);

    // Delete the whole buffer's content -- the <script> block, and every
    // embedded region with it, is gone.
    buffer.SetPoint(0);
    buffer.DeleteRange(0, buffer.Text().size());

    view.Paint(canvas);

    const std::string jsClose = ReadRawLspFrame(jsServer.serverStdinRead);
    REQUIRE(jsClose.find("textDocument/didClose") != std::string::npos);

    std::filesystem::remove(path);
}
