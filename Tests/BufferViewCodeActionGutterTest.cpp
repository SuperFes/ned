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

// The quick-fix gutter column: a marker beside a diagnostic the server says
// it can fix. Painted-screen tests rather than model ones, because the two
// things that can go wrong here are both about pixels -- the marker has to
// reach a cell at all, and the column it occupies has to be reserved whether
// or not anything is in it, or every column right of it slides sideways as
// fixes come and go while typing.

using ned::text::Buffer;
using ned::ui::BufferView;
using Json       = nlohmann::json;
using Diagnostic = ned::text::Buffer::Diagnostic;

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

namespace {

// The painted row as a plain string, gutter included.
std::string PaintedRow(ned::ui::Screen& screen, int row, int width = 40) {
    std::string painted;
    for (int x = 0; x < width; ++x) {
        painted += screen.PixelAt(x, row).character;
    }
    return painted;
}

// Drives one viewport-scoped codeAction exchange: answers whatever the view
// asked for with these actions, then repaints.
void Repaint(LspFixture& lsp, BufferView& view) {
    ned::ui::Canvas canvas(lsp.screen, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 5});
    view.Paint(canvas);
}

void AnswerCodeActions(LspFixture& lsp, BufferView& view, FrameReader& frames, const Json& actions) {
    const Json request = frames.WithMethod("textDocument/codeAction");
    REQUIRE(request.contains("id"));
    lsp.client->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"id", request["id"]}, {"result", actions}}.dump());
    ned::ui::Canvas canvas(lsp.screen, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 5});
    view.Paint(canvas);
}

Json QuickFix(const char* title, int line, int startChar, int endChar) {
    return Json{{"title", title},
                {"kind", "quickfix"},
                {"diagnostics", Json::array({Json{{"range", RangeJson(line, startChar, endChar)}}})}};
}

} // namespace

TEST_CASE("The quick-fix marker is painted for a line the server offered a fix on", "[BufferView][CodeAction]") {
    LspFixture lsp("ned_bufferview_code_action_gutter_test.c", "int a = 1;\nint main() {}\nint b = 2;\n");
    lsp.SourceBuffer().SetDiagnostics({Diagnostic{.startByte = lsp.SourceBuffer().Content().LineToByteOffset(1),
                                                  .endByte   = lsp.SourceBuffer().Content().LineToByteOffset(1) + 3,
                                                  .severity  = Diagnostic::Severity::Warning,
                                                  .message   = "unused"}});
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);
    FrameReader   frames{server.serverStdinRead, {}};

    BufferView view = lsp.fixture.View();
    lsp.Ready(view, frames);
    AnswerCodeActions(lsp, view, frames, Json::array({QuickFix("remove it", 1, 0, 3)}));

    // Row 1 is line 1, the flagged line. The severity glyph and the
    // quick-fix marker are adjacent columns, in that order.
    REQUIRE(PaintedRow(lsp.screen, 1).find("▲✦") != std::string::npos);
    // And nothing was drawn on the lines that carry no diagnostic at all.
    REQUIRE(PaintedRow(lsp.screen, 0).find("✦") == std::string::npos);
    REQUIRE(PaintedRow(lsp.screen, 2).find("✦") == std::string::npos);
}

TEST_CASE("A diagnostic the server offers no fix for gets the severity glyph alone", "[BufferView][CodeAction]") {
    LspFixture lsp("ned_bufferview_code_action_gutter_nofix_test.c", "int a = 1;\nint main() {}\nint b = 2;\n");
    lsp.SourceBuffer().SetDiagnostics({Diagnostic{.startByte = lsp.SourceBuffer().Content().LineToByteOffset(1),
                                                  .endByte   = lsp.SourceBuffer().Content().LineToByteOffset(1) + 3,
                                                  .severity  = Diagnostic::Severity::Warning,
                                                  .message   = "unused"}});
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);
    FrameReader   frames{server.serverStdinRead, {}};

    BufferView view = lsp.fixture.View();
    lsp.Ready(view, frames);
    AnswerCodeActions(lsp, view, frames, Json::array());

    REQUIRE(PaintedRow(lsp.screen, 1).find("▲") != std::string::npos); // the diagnostic is still marked
    REQUIRE(PaintedRow(lsp.screen, 1).find("✦") == std::string::npos); // nothing claims it is fixable
}

// The whole reason the column is reserved on "a server has this buffer open"
// rather than on "there is a marker right now": the line numbers and the
// text must not slide sideways the moment a fix appears or is applied.
TEST_CASE("The quick-fix column is reserved whether or not it has anything in it", "[BufferView][CodeAction]") {
    LspFixture lsp("ned_bufferview_code_action_gutter_width_test.c", "int a = 1;\nint main() {}\nint b = 2;\n");
    lsp.SourceBuffer().SetDiagnostics({Diagnostic{.startByte = lsp.SourceBuffer().Content().LineToByteOffset(1),
                                                  .endByte   = lsp.SourceBuffer().Content().LineToByteOffset(1) + 3,
                                                  .severity  = Diagnostic::Severity::Warning,
                                                  .message   = "unused"}});
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);
    FrameReader   frames{server.serverStdinRead, {}};

    BufferView view = lsp.fixture.View();
    lsp.Ready(view, frames);
    // The column is reserved from the first frame after the server has the
    // document open -- Ready's own paint is where didOpen goes out, so the
    // frame that settles the reservation is the one after it.
    Repaint(lsp, view);
    const std::string before = PaintedRow(lsp.screen, 0);

    AnswerCodeActions(lsp, view, frames, Json::array({QuickFix("remove it", 1, 0, 3)}));

    // Line 0 has no marker either way, so its whole row -- gutter and text
    // alike -- must be byte-identical across a response that lit line 1.
    REQUIRE(PaintedRow(lsp.screen, 0) == before);

    // The other half of the invariant -- the marker retiring when the
    // diagnostic it belongs to goes away -- is a Manager-level fact
    // ("A viewport whose diagnostics have gone away clears the hints it
    // had"), not a paint one: reaching it from here would mean driving a
    // whole debounced re-sync just to watch one cell empty.
}

TEST_CASE("A click on the quick-fix column moves point onto the flagged range", "[BufferView][CodeAction]") {
    LspFixture        lsp("ned_bufferview_code_action_gutter_click_test.c", "int a = 1;\nint main() {}\nint b = 2;\n");
    Buffer&           buffer  = lsp.SourceBuffer();
    const std::size_t flagged = buffer.Content().LineToByteOffset(1) + 4;
    buffer.SetDiagnostics({Diagnostic{.startByte = flagged,
                                      .endByte   = flagged + 4,
                                      .severity  = Diagnostic::Severity::Warning,
                                      .message   = "unused"}});
    FakeLspServer server = FakeLspServer::Create(lsp.manager, "c", lsp.eventLoop, lsp.client);
    FrameReader   frames{server.serverStdinRead, {}};

    BufferView view = lsp.fixture.View();
    lsp.Ready(view, frames);
    AnswerCodeActions(lsp, view, frames, Json::array({QuickFix("remove it", 1, 4, 8)}));

    // The column the marker actually landed in, read off the painted row --
    // the click has to hit the same cell the user sees, not a recomputed
    // guess at where it should be.
    int markerColumn = -1;
    for (int x = 0; x < 40; ++x) {
        if (lsp.screen.PixelAt(x, 1).character == "✦") {
            markerColumn = x;
            break;
        }
    }
    REQUIRE(markerColumn >= 0);

    buffer.SetPoint(0);
    view.OnEvent(ned::ui::test::Mouse(markerColumn, 1, ned::ui::MouseEvent::Button::Left,
                                      ned::ui::MouseEvent::Motion::Pressed));

    // Point landed on the diagnostic's own start, not on column 0 of the
    // line -- the request the fix comes from is a request about point.
    REQUIRE(buffer.Point() == flagged);
}
