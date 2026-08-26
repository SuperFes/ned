#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Lsp/LspClient.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Lsp/Transport.h"
#include "Editor/Mode.h"
#include "Editor/Multibuffer.h"
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
#include "UI/Widget.h"

using ned::editor::multibuffer::ClearRegistryForTesting;
using ned::editor::multibuffer::MultibufferIndexFor;
using ned::text::Buffer;
using ned::ui::BufferView;

namespace {

// Mirrors MultibufferTest.cpp's own RegistryResetGuard -- without this, a
// Buffer destroyed at the end of one TEST_CASE can leave a stale registry
// entry a later TEST_CASE's freshly allocated Buffer spuriously "inherits"
// if the allocator reuses the same address.
struct RegistryResetGuard {
    RegistryResetGuard() {
        ClearRegistryForTesting();
    }
    ~RegistryResetGuard() {
        ClearRegistryForTesting();
    }
};

// Mirrors BufferViewDiagnosticsBufferTest.cpp's own Fixture exactly.
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
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

// find-all-references follow-up: ProjectRoot is process-wide state -- every
// test that sets one must restore it afterward, guaranteed via RAII (a
// failed REQUIRE partway through would skip a manual reset). Mirrors
// BufferViewDiffGutterTest.cpp's own inline save/restore.
struct ProjectRootGuard {
    std::filesystem::path previous = ned::editor::ProjectRoot();
    ~ProjectRootGuard() {
        ned::editor::SetProjectRoot(previous);
    }
};

// find-references follow-up: mirrors BufferViewTest.cpp's own
// FakeLspServer/ReadRawLspFrame exactly -- kept file-local here too, same
// "not worth a new cross-translation-unit dependency for something this
// small" precedent that file's own doc comment already states.
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

} // namespace

TEST_CASE("RequestProjectFindReferences finds every whole-word match across the project, in one *references* multibuffer",
          "[BufferView][ProjectFindReferences]") {
    const ProjectRootGuard rootGuard;

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_find_references_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.cpp") << "int widget = 1;\nint other = widget + 1;\n";
    }
    {
        std::ofstream(dir / "b.cpp") << "void UseWidget(int widget) {}\n"; // "UseWidget" must NOT match -- not a whole word
    }
    ned::editor::SetProjectRoot(dir);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("int widget = 1;\n");
    fixture.buffer.SetPoint(5); // inside "widget"

    BufferView view = fixture.View();
    view.RequestProjectFindReferencesForTesting();

    Buffer* results = fixture.bufferList.Find("*references: widget*");
    REQUIRE(results != nullptr);
    // Editable-multibuffer follow-up: find-references excerpts are
    // editable, so the composite is no longer whole-buffer read-only --
    // chrome (headers/rules) stays protected via ExcerptRanges()' own
    // point-level enforcement instead.
    REQUIRE_FALSE(results->ReadOnly());
    REQUIRE(results->ExcerptRanges().size() == 3);

    auto* index = MultibufferIndexFor(*results);
    REQUIRE(index != nullptr);
    // a.cpp has two whole-word occurrences of "widget" (the declaration and
    // the later use); b.cpp's "UseWidget"/parameter named "widget" -- only
    // the parameter is a whole word, "UseWidget" itself must not match.
    REQUIRE(index->Spans().size() == 3);

    REQUIRE(fixture.statusMessage.find("3 references to \"widget\"") == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("RequestProjectFindReferences reports no identifier at point without building a buffer", "[BufferView][ProjectFindReferences]") {
    const ProjectRootGuard rootGuard;

    Fixture fixture;
    fixture.buffer.InsertAtPoint("   ");
    fixture.buffer.SetPoint(1); // sits on whitespace, not a word

    BufferView view = fixture.View();
    view.RequestProjectFindReferencesForTesting();

    REQUIRE(fixture.statusMessage == "No identifier at point.");
}

// find-references follow-up: with a language server actually running for
// the buffer, project-find-references must send a real
// textDocument/references (with includeDeclaration) instead of falling
// back to the RE2 text scan the two tests above exercise, and build the
// same "*references: <word>*" multibuffer from the resolved locations.
TEST_CASE("RequestProjectFindReferences sends textDocument/references when a language server is running",
          "[BufferView][ProjectFindReferences][Lsp]") {
    const ProjectRootGuard rootGuard;

    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_find_references_lsp_test.cpp";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int widget = 1;\n");
    buffer.SetPoint(5); // inside "widget"
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
    view.Paint(canvas);                            // syncs the buffer -- see SyncBuffer's own didOpen
    (void)ReadRawLspFrame(server.serverStdinRead); // drain didOpen

    view.RequestProjectFindReferencesForTesting();

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/references");
    REQUIRE(request["params"]["context"]["includeDeclaration"] == true);

    const std::filesystem::path targetPath = std::filesystem::temp_directory_path() / "ned_find_references_lsp_target_test.cpp";
    std::filesystem::remove(targetPath);
    {
        std::ofstream(targetPath) << "int other = widget + 1;\n";
    }
    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", ned::editor::lsp::Json::array(
                       {{{"uri", "file://" + targetPath.string()},
                         {"range", {{"start", {{"line", 0}, {"character", 12}}}, {"end", {{"line", 0}, {"character", 18}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    Buffer* results = fixture.bufferList.Find("*references: widget*");
    REQUIRE(results != nullptr);
    REQUIRE(results->ExcerptRanges().size() == 1);
    REQUIRE(fixture.statusMessage.find("1 reference to \"widget\"") == 0);

    std::filesystem::remove(targetPath);
}
