#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <unistd.h>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Lsp/LspClient.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Lsp/Transport.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EventLoop.h"
#include "UI/Theme.h"

// documentLink follow-up: open-link-at-point's LSP-first tier. Everything
// here exercises the real BufferView entry point (M-x open-link-at-point)
// against a fake language server, since the whole feature is about which
// tier answers and what happens when one declines -- not about parsing,
// which LspContentTest/LspManagerTest already cover directly.

using ned::text::Buffer;
using ned::ui::BufferView;
using Json = nlohmann::json;

namespace {

// Mirrors BufferViewEmbeddedLspTest.cpp's own Fixture, except mode defaults
// to CMode -- #include is the canonical documentLink case (clangd resolves
// one through compile_commands.json, which no filesystem heuristic can).
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
    ned::editor::Mode            mode  = ned::editor::CMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme);
    }
};

// Mirrors BufferViewEmbeddedLspTest.cpp's own FakeLspServer/ReadRawLspFrame
// (kept file-local there too -- same precedent).
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
        auto client = std::make_unique<ned::editor::lsp::LspClient>(
            ned::editor::lsp::Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop);
        outClient = &manager.SetClientForTesting(language, std::move(client));
        return FakeLspServer(clientWritesHere[0], clientReadsHere[1]);
    }
};

// Unlike BufferViewEmbeddedLspTest.cpp's own single-shot ReadRawLspFrame,
// this keeps the bytes past one frame's Content-Length instead of
// discarding them: a single Paint() here emits several frames in quick
// succession (didOpen, then whatever background requests the mode's own
// capabilities trigger), and one ::read routinely returns more than one of
// them, so a frame reader that forgets its own tail would truncate or
// mis-parse the very next one.
struct FrameReader {
    int         fd;
    std::string pending;

    // The next complete frame's parsed JSON, or an empty object once the fd
    // has nothing more to give.
    Json Next() {
        for (int i = 0; i < 8; ++i) {
            if (const std::optional<Json> framed = TakeFrame()) {
                return *framed;
            }
            char          buffer[4096];
            const ssize_t n = ::read(fd, buffer, sizeof(buffer));
            if (n <= 0) {
                break;
            }
            pending.append(buffer, static_cast<std::size_t>(n));
        }
        const std::optional<Json> framed = TakeFrame();
        return framed ? *framed : Json::object();
    }

    // Reads frames until one whose "method" matches, or the fd runs dry.
    Json WithMethod(std::string_view method) {
        for (int i = 0; i < 8; ++i) {
            const Json frame = Next();
            if (frame.empty()) {
                break;
            }
            if (frame.value("method", std::string()) == method) {
                return frame;
            }
        }
        return Json::object();
    }

  private:
    std::optional<Json> TakeFrame() {
        const auto headerEnd = pending.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            return std::nullopt;
        }
        const std::string_view kPrefix   = "Content-Length: ";
        const auto             prefixPos = pending.find(kPrefix);
        if (prefixPos == std::string::npos || prefixPos > headerEnd) {
            return std::nullopt;
        }
        const std::size_t contentLength = std::stoul(pending.substr(prefixPos + kPrefix.size()));
        const std::size_t bodyStart     = headerEnd + 4;
        if (pending.size() < bodyStart + contentLength) {
            return std::nullopt;
        }
        const Json frame = Json::parse(pending.substr(bodyStart, contentLength));
        pending.erase(0, bodyStart + contentLength);
        return frame;
    }
};

void TypeText(BufferView& view, std::string_view text) {
    for (const char ch : text) {
        view.OnEvent(ned::ui::test::Character(std::string(1, ch)));
    }
}

void InvokeOpenLinkAtPoint(BufferView& view) {
    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "open-link-at-point");
    view.OnEvent(ned::ui::test::Return());
}

void WriteFile(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream out(path);
    out << contents;
}

Json RangeJson(int line, int startChar, int endChar) {
    return Json{{"start", {{"line", line}, {"character", startChar}}}, {"end", {{"line", line}, {"character", endChar}}}};
}

// One BufferView wired to a fake "c" server, with an already-drained
// didOpen -- the shared setup every test below needs.
struct LspFixture {
    Fixture                      fixture;
    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager{fixture.bufferList, eventLoop};
    ned::editor::lsp::LspClient* client = nullptr;
    ned::ui::Screen              screen{80, 6};

    std::filesystem::path sourcePath;

    explicit LspFixture(const std::string& basename, std::string_view contents) {
        sourcePath     = std::filesystem::temp_directory_path() / basename;
        Buffer& buffer = fixture.bufferList.OpenOrCreateFile(sourcePath);
        buffer.InsertAtPoint(std::string(contents));
        fixture.activeBuffer.Set(buffer);
    }

    ~LspFixture() {
        std::filesystem::remove(sourcePath);
    }

    LspFixture(const LspFixture&)            = delete;
    LspFixture& operator=(const LspFixture&) = delete;

    Buffer& SourceBuffer() {
        return fixture.bufferList.OpenOrCreateFile(sourcePath);
    }

    // Wires the view up and paints once (which is what syncs the buffer to
    // the server), draining the resulting didOpen. Takes the view by
    // reference rather than returning one -- BufferView is deliberately
    // neither copyable nor movable.
    void Ready(BufferView& view, FrameReader& frames) {
        view.SetLspManager(&manager);
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 5});
        ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 5});
        view.Paint(canvas);
        (void)frames.WithMethod("textDocument/didOpen");
    }
};

} // namespace

TEST_CASE("open-link-at-point follows a server-reported documentLink to its file target", "[BufferView][DocumentLink]") {
    LspFixture    lsp("ned_bufferview_document_link_test.c", "#include \"dl-target.h\"\nint main() {}\n");
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);

    // A header the filesystem heuristic could never find on its own: it
    // lives in a subdirectory nothing in the buffer names, exactly the
    // situation compile_commands.json-driven resolution exists for.
    const std::filesystem::path includeDir = std::filesystem::temp_directory_path() / "ned-document-link-include";
    std::filesystem::create_directories(includeDir);
    const std::filesystem::path target = includeDir / "dl-target.h";
    WriteFile(target, "#pragma once\n");

    FrameReader frames{server.serverStdinRead};
    BufferView  view = lsp.fixture.View();
    lsp.Ready(view, frames);
    lsp.SourceBuffer().SetPoint(12); // inside the "dl-target.h" token
    InvokeOpenLinkAtPoint(view);

    const Json request = frames.WithMethod("textDocument/documentLink");
    REQUIRE(request["method"] == "textDocument/documentLink");

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", request["id"]},
        {"result", Json::array({{{"range", RangeJson(0, 9, 22)}, {"target", "file://" + target.string()}}})},
    };
    lsp.client->DispatchFrame(response.dump());

    REQUIRE(lsp.fixture.activeBuffer.Get().Path().has_value());
    REQUIRE(*lsp.fixture.activeBuffer.Get().Path() == target);

    std::filesystem::remove_all(includeDir);
}

TEST_CASE("open-link-at-point falls back to filesystem resolution when no documentLink covers point",
          "[BufferView][DocumentLink]") {
    LspFixture    lsp("ned_bufferview_document_link_fallback_test.c", "#include \"ned-dl-fallback.h\"\nint main() {}\n");
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);

    // Sits beside the source file, which is where the pre-LSP tier looks.
    const std::filesystem::path sibling = std::filesystem::temp_directory_path() / "ned-dl-fallback.h";
    WriteFile(sibling, "#pragma once\n");

    FrameReader frames{server.serverStdinRead};
    BufferView  view = lsp.fixture.View();
    lsp.Ready(view, frames);
    lsp.SourceBuffer().SetPoint(12);
    InvokeOpenLinkAtPoint(view);

    const Json request = frames.WithMethod("textDocument/documentLink");
    REQUIRE(request["method"] == "textDocument/documentLink");

    // The server answers, but only about a range on the *second* line --
    // nothing covering point.
    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", request["id"]},
        {"result", Json::array({{{"range", RangeJson(1, 0, 3)}, {"target", "file:///nowhere/unused.h"}}})},
    };
    lsp.client->DispatchFrame(response.dump());

    REQUIRE(lsp.fixture.activeBuffer.Get().Path().has_value());
    REQUIRE(*lsp.fixture.activeBuffer.Get().Path() == sibling);

    std::filesystem::remove(sibling);
}

TEST_CASE("open-link-at-point falls back when the server names a path that isn't there", "[BufferView][DocumentLink]") {
    LspFixture    lsp("ned_bufferview_document_link_missing_test.c", "#include \"ned-dl-missing.h\"\nint main() {}\n");
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);

    const std::filesystem::path sibling = std::filesystem::temp_directory_path() / "ned-dl-missing.h";
    WriteFile(sibling, "#pragma once\n");

    FrameReader frames{server.serverStdinRead};
    BufferView  view = lsp.fixture.View();
    lsp.Ready(view, frames);
    lsp.SourceBuffer().SetPoint(12);
    InvokeOpenLinkAtPoint(view);

    const Json request  = frames.WithMethod("textDocument/documentLink");
    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", request["id"]},
        {"result", Json::array({{{"range", RangeJson(0, 9, 27)}, {"target", "file:///nonexistent/ned-dl-missing.h"}}})},
    };
    lsp.client->DispatchFrame(response.dump());

    // A stale server answer must not cost the user the heuristic (and must
    // never create an empty buffer at the phantom path either).
    REQUIRE(lsp.fixture.activeBuffer.Get().Path().has_value());
    REQUIRE(*lsp.fixture.activeBuffer.Get().Path() == sibling);

    std::filesystem::remove(sibling);
}

TEST_CASE("open-link-at-point resolves a target-less documentLink before following it", "[BufferView][DocumentLink]") {
    LspFixture    lsp("ned_bufferview_document_link_resolve_test.c", "#include \"dl-resolved.h\"\nint main() {}\n");
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);

    const std::filesystem::path includeDir = std::filesystem::temp_directory_path() / "ned-document-link-resolve-include";
    std::filesystem::create_directories(includeDir);
    const std::filesystem::path target = includeDir / "dl-resolved.h";
    WriteFile(target, "#pragma once\n");

    FrameReader frames{server.serverStdinRead};
    BufferView  view = lsp.fixture.View();
    lsp.Ready(view, frames);
    lsp.SourceBuffer().SetPoint(12);
    InvokeOpenLinkAtPoint(view);

    const Json listRequest  = frames.WithMethod("textDocument/documentLink");
    const Json listItem     = {{"range", RangeJson(0, 9, 24)}, {"data", {{"token", 7}}}};
    const Json listResponse = {
        {"jsonrpc", "2.0"},
        {"id", listRequest["id"]},
        {"result", Json::array({listItem})},
    };
    lsp.client->DispatchFrame(listResponse.dump());

    // Nothing has moved yet -- the link had no target to follow.
    REQUIRE(*lsp.fixture.activeBuffer.Get().Path() == lsp.sourcePath);

    const Json resolveRequest = frames.WithMethod("documentLink/resolve");
    REQUIRE(resolveRequest["method"] == "documentLink/resolve");
    REQUIRE(resolveRequest["params"]["data"]["token"] == 7); // the original item, round-tripped verbatim

    Json resolved              = listItem;
    resolved["target"]         = "file://" + target.string();
    const Json resolveResponse = {
        {"jsonrpc", "2.0"},
        {"id", resolveRequest["id"]},
        {"result", resolved},
    };
    lsp.client->DispatchFrame(resolveResponse.dump());

    REQUIRE(*lsp.fixture.activeBuffer.Get().Path() == target);

    std::filesystem::remove_all(includeDir);
}

TEST_CASE("open-link-at-point ignores a documentLink response that arrived after point moved", "[BufferView][DocumentLink]") {
    LspFixture    lsp("ned_bufferview_document_link_stale_test.c", "#include \"dl-stale.h\"\nint main() {}\n");
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);

    const std::filesystem::path includeDir = std::filesystem::temp_directory_path() / "ned-document-link-stale-include";
    std::filesystem::create_directories(includeDir);
    const std::filesystem::path target = includeDir / "dl-stale.h";
    WriteFile(target, "#pragma once\n");

    FrameReader frames{server.serverStdinRead};
    BufferView  view = lsp.fixture.View();
    lsp.Ready(view, frames);
    lsp.SourceBuffer().SetPoint(12);
    InvokeOpenLinkAtPoint(view);

    const Json request = frames.WithMethod("textDocument/documentLink");
    lsp.SourceBuffer().SetPoint(0); // the user moved on before the server answered

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", request["id"]},
        {"result", Json::array({{{"range", RangeJson(0, 9, 21)}, {"target", "file://" + target.string()}}})},
    };
    lsp.client->DispatchFrame(response.dump());

    REQUIRE(*lsp.fixture.activeBuffer.Get().Path() == lsp.sourcePath); // no jump, and no fallback either

    std::filesystem::remove_all(includeDir);
}
