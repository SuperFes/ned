#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <unistd.h>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Lsp/Client.h"
#include "Editor/Lsp/Manager.h"
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

// codeLens draws its annotation row ABOVE the line it annotates, which is
// the only row kind that shifts its own line's content down rather than the
// lines after it. Everything here is about that one asymmetry: the row math
// that maps point to a screen row has to skip its own line's leading row,
// where every other consumer only has to count the rows of the lines before
// it. Live-reported 2026-09-19 against fish-lsp, the first server in use
// here that actually returns lenses.

using ned::text::Buffer;
using ned::ui::BufferView;
using Json = nlohmann::json;

namespace {

// Mirrors BufferViewDocumentLinkTest.cpp's own Fixture -- same shape, same
// reasons; see that file for why each piece is here.
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

    static FakeLspServer Create(ned::editor::lsp::Manager& manager, const std::string& language, ned::ui::EventLoop& eventLoop,
                                ned::editor::lsp::Client*& outClient) {
        int clientWritesHere[2];
        int clientReadsHere[2];
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        auto client = std::make_unique<ned::editor::lsp::Client>(
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

Json RangeJson(int line, int startChar, int endChar) {
    return Json{{"start", {{"line", line}, {"character", startChar}}}, {"end", {{"line", line}, {"character", endChar}}}};
}

// One BufferView wired to a fake "c" server, with an already-drained
// didOpen -- the shared setup every test below needs.
struct LspFixture {
    Fixture                   fixture;
    ned::ui::EventLoop        eventLoop;
    ned::editor::lsp::Manager manager{fixture.bufferList, eventLoop};
    ned::editor::lsp::Client* client = nullptr;
    ned::ui::Screen           screen{80, 6};

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

TEST_CASE("CursorPosition skips the code lens row drawn above point's own line", "[BufferView][CodeLens]") {
    LspFixture    lsp("ned_bufferview_code_lens_row_test.c", "int a = 1;\nint main() {}\nint b = 2;\n");
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);
    FrameReader   frames{server.serverStdinRead, {}};

    Buffer&    buffer = lsp.SourceBuffer();
    BufferView view   = lsp.fixture.View();
    lsp.Ready(view, frames);

    // Point on line 1, the line the lens will annotate.
    buffer.SetPoint(buffer.Content().LineToByteOffset(1) + 4);

    const Json request = frames.WithMethod("textDocument/codeLens");
    REQUIRE(request.contains("id"));
    lsp.client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                                   {"id", request["id"]},
                                   {"result", Json::array({Json{{"range", RangeJson(1, 0, 3)},
                                                                {"command", {{"title", "2 references"}, {"command", "noop"}}}}})}}
                                  .dump());

    ned::ui::Canvas canvas(lsp.screen, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 5});
    view.Paint(canvas);

    // Row 0 is line 0, row 1 is the lens drawn above line 1, and line 1's
    // own text is row 2. Reporting row 1 puts the terminal cursor on the
    // annotation instead of on the character it is editing.
    const std::optional<ned::ui::Point> cursor = view.CursorPosition();
    REQUIRE(cursor.has_value());
    REQUIRE(cursor->y == 2);
}

TEST_CASE("A code lens row is drawn with its own marker glyph", "[BufferView][CodeLens]") {
    LspFixture    lsp("ned_bufferview_code_lens_glyph_test.c", "int a = 1;\nint main() {}\nint b = 2;\n");
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);
    FrameReader   frames{server.serverStdinRead, {}};

    BufferView view = lsp.fixture.View();
    lsp.Ready(view, frames);

    const Json request = frames.WithMethod("textDocument/codeLens");
    REQUIRE(request.contains("id"));
    lsp.client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                                   {"id", request["id"]},
                                   {"result", Json::array({Json{{"range", RangeJson(1, 0, 3)},
                                                                {"command", {{"title", "2 references"}, {"command", "noop"}}}}})}}
                                  .dump());

    ned::ui::Canvas canvas(lsp.screen, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 5});
    view.Paint(canvas);

    // Row 1 is the lens drawn above line 1. Reading the painted screen
    // rather than the model: the glyph only matters if it actually reaches
    // a cell, and the gutter offset is part of getting that right.
    std::string painted;
    for (int x = 0; x < 40; ++x) {
        painted += lsp.screen.PixelAt(x, 1).character;
    }
    REQUIRE(painted.find("\u25B9 2 references") != std::string::npos);
}

TEST_CASE("CursorPosition is unaffected when the lens sits on a line other than point's", "[BufferView][CodeLens]") {
    LspFixture    lsp("ned_bufferview_code_lens_row_other_test.c", "int a = 1;\nint main() {}\nint b = 2;\n");
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);
    FrameReader   frames{server.serverStdinRead, {}};

    Buffer&    buffer = lsp.SourceBuffer();
    BufferView view   = lsp.fixture.View();
    lsp.Ready(view, frames);

    buffer.SetPoint(buffer.Content().LineToByteOffset(0) + 4); // point on line 0, lens on line 1

    const Json request = frames.WithMethod("textDocument/codeLens");
    REQUIRE(request.contains("id"));
    lsp.client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                                   {"id", request["id"]},
                                   {"result", Json::array({Json{{"range", RangeJson(1, 0, 3)},
                                                                {"command", {{"title", "2 references"}, {"command", "noop"}}}}})}}
                                  .dump());

    ned::ui::Canvas canvas(lsp.screen, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 5});
    view.Paint(canvas);

    // A lens BELOW point changes nothing above it -- the guard against
    // "fixed" becoming "added the row unconditionally".
    const std::optional<ned::ui::Point> cursor = view.CursorPosition();
    REQUIRE(cursor.has_value());
    REQUIRE(cursor->y == 0);
}

// A lens is allowed to arrive with no command at all, to be filled in by
// codeLens/resolve. jdtls answers EVERY lens that way -- range and data, no
// command -- so this shape is not an edge case, it is what Java looks like.
// Counting a row for one of those reserved a row the paint then skipped,
// which is how a blank line appeared between an annotation and the method
// under it (live-reported 2026-09-22 against jdtls).
TEST_CASE("An unresolved lens gets no row, and the resolve it triggers brings one", "[BufferView][CodeLens]") {
    LspFixture    lsp("ned_bufferview_code_lens_unresolved_test.c", "int a = 1;\nint main() {}\nint b = 2;\n");
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);
    FrameReader   frames{server.serverStdinRead, {}};

    Buffer&    buffer = lsp.SourceBuffer();
    BufferView view   = lsp.fixture.View();
    lsp.Ready(view, frames);

    buffer.SetPoint(buffer.Content().LineToByteOffset(1) + 4); // point on the annotated line

    const Json request = frames.WithMethod("textDocument/codeLens");
    REQUIRE(request.contains("id"));
    const Json unresolved{{"range", RangeJson(1, 0, 3)}, {"data", Json::array({"references"})}};
    lsp.client->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", request["id"]}, {"result", Json::array({unresolved})}}.dump());

    ned::ui::Canvas canvas(lsp.screen, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 5});
    view.Paint(canvas);

    // Nothing to say yet, so no row: line 1's own text is still row 1.
    const std::optional<ned::ui::Point> beforeResolve = view.CursorPosition();
    REQUIRE(beforeResolve.has_value());
    REQUIRE(beforeResolve->y == 1);

    // ...and the lens is not simply dropped: the resolve goes out on its own,
    // carrying the server's own lens object back verbatim.
    const Json resolve = frames.WithMethod("codeLens/resolve");
    REQUIRE(resolve.contains("id"));
    REQUIRE(resolve["params"] == unresolved);

    lsp.client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                                   {"id", resolve["id"]},
                                   {"result", Json{{"range", RangeJson(1, 0, 3)},
                                                   {"command", {{"title", "2 references"}, {"command", "noop"}}}}}}
                                  .dump());
    view.Paint(canvas);

    const std::optional<ned::ui::Point> afterResolve = view.CursorPosition();
    REQUIRE(afterResolve.has_value());
    REQUIRE(afterResolve->y == 2);

    std::string painted;
    for (int x = 0; x < 40; ++x) {
        painted += lsp.screen.PixelAt(x, 1).character;
    }
    REQUIRE(painted.find("▹ 2 references") != std::string::npos);
}

// A lens row is chrome drawn beside a line, not a line of its own -- the
// fold column's vertical bar has to run through it, or a block reads as two
// separate blocks with a gap in the middle.
TEST_CASE("The fold column's bar runs through a code lens row", "[BufferView][CodeLens]") {
    LspFixture    lsp("ned_bufferview_code_lens_fold_bar_test.c", "int main(void) {\n    return 0;\n}\n");
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);
    FrameReader   frames{server.serverStdinRead, {}};

    BufferView view = lsp.fixture.View();
    lsp.Ready(view, frames);

    const Json request = frames.WithMethod("textDocument/codeLens");
    REQUIRE(request.contains("id"));
    lsp.client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                                   {"id", request["id"]},
                                   {"result", Json::array({Json{{"range", RangeJson(1, 4, 10)},
                                                                {"command", {{"title", "1 reference"}, {"command", "noop"}}}}})}}
                                  .dump());

    ned::ui::Canvas canvas(lsp.screen, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 5});
    view.Paint(canvas);

    // Row 0 is the function's own header line, which carries the expanded
    // glyph -- found by scanning rather than by recomputing the gutter
    // layout's arithmetic a second time.
    int foldColumn = -1;
    for (int x = 0; x < 40; ++x) {
        if (lsp.screen.PixelAt(x, 0).character == "⊟") {
            foldColumn = x;
            break;
        }
    }
    REQUIRE(foldColumn >= 0);

    // Row 1 is the lens drawn above line 1, which sits inside the block.
    REQUIRE(lsp.screen.PixelAt(foldColumn, 1).character == "│");
    // Row 2 is line 1's own text, still inside it.
    REQUIRE(lsp.screen.PixelAt(foldColumn, 2).character == "│");
}
