#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <unistd.h>

#include "Editor/BackgroundActivity.h"
#include "Editor/Lsp/LspClient.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Lsp/LspServerConfig.h"
#include "Editor/Lsp/Transport.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/ActiveBuffer.h"
#include "UI/EventLoop.h"
#include "UI/ModeLine.h"
#include "UI/Theme.h"

namespace {

std::string RowText(ned::ui::Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

ned::ui::Screen MakeScreen(int width, int height) {
    return ned::ui::Screen(width, height);
}

// mode-line-lsp-indicator follow-up: registers a fake, already-"running"
// client for language via LspManager::SetClientForTesting, mirroring
// LspManagerTest.cpp's own FakeServer -- a raw pipe pair standing in for a
// real language server, with nothing read from or written to it here (these
// tests only care that LspManager::StatusForLanguage reports Running, not
// about any real request/response traffic). Closing the "server" side's write end
// (clientReadsHere[1]) right away, same as FakeServer's own destructor,
// matters even though nothing is read from it in these tests: without an
// EOF, the client's background read thread blocks in Transport::ReadFrame
// forever, and LspClient's destructor -- which joins that thread -- then
// hangs the whole test binary at LspManager's teardown (confirmed: this
// exact omission hung ned_tests with zero output).
void RegisterFakeRunningClient(ned::editor::lsp::LspManager& manager, const std::string& language, ned::ui::EventLoop& eventLoop) {
    int clientWritesHere[2];
    int clientReadsHere[2];
    REQUIRE(::pipe(clientWritesHere) == 0);
    REQUIRE(::pipe(clientReadsHere) == 0);
    auto client = std::make_unique<ned::editor::lsp::LspClient>(
        ned::editor::lsp::Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop);
    manager.SetClientForTesting(language, std::move(client));
    ::close(clientReadsHere[1]);
    ::close(clientWritesHere[0]);
}

// embedded-language-documents follow-up: unlike RegisterFakeRunningClient
// above (whose own doc comment says "nothing read from or written to it
// here"), a test that calls SyncBuffer/SyncEmbeddedDocuments makes the
// client send a real textDocument/didOpen notification -- writing into a
// pipe whose read end RegisterFakeRunningClient already closed raises
// SIGPIPE and kills the whole test binary (confirmed live: this exact
// mismatch did precisely that). Mirrors LspManagerTest.cpp's own FakeServer:
// keeps both fds open (RAII-closed on destruction) so a real didOpen write
// has somewhere to land.
struct FakeServer {
    int serverStdinRead;
    int serverStdoutWrite;

    FakeServer(int readFd, int writeFd) : serverStdinRead(readFd), serverStdoutWrite(writeFd) {
    }
    ~FakeServer() {
        ::close(serverStdoutWrite);
        ::close(serverStdinRead);
    }
    FakeServer(const FakeServer&)            = delete;
    FakeServer& operator=(const FakeServer&) = delete;
    FakeServer(FakeServer&&)                 = default;

    static FakeServer Create(ned::editor::lsp::LspManager& manager, const std::string& language, ned::ui::EventLoop& eventLoop) {
        int clientWritesHere[2];
        int clientReadsHere[2];
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        auto client = std::make_unique<ned::editor::lsp::LspClient>(
            ned::editor::lsp::Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop);
        manager.SetClientForTesting(language, std::move(client));
        return FakeServer(clientWritesHere[0], clientReadsHere[1]);
    }
};

} // namespace

TEST_CASE("ModeLine shows a live load percentage when the loader published progress", "[ModeLine]") {
    ned::text::Buffer buffer("huge.txt");
    buffer.MarkLoading();

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(60, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 0});

    // No progress published (or an unknown total): the plain indicator.
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("Loading...") != std::string::npos);
    REQUIRE(RowText(screen, 0, 60).find('%') == std::string::npos);

    auto progress        = std::make_shared<ned::text::LoadProgress>();
    progress->totalBytes = 200;
    progress->bytesRead.store(50);
    buffer.SetLoadProgress(progress);
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("Loading... 25%") != std::string::npos);

    // bytesRead past totalBytes (the file grew mid-load) clamps to 100.
    progress->bytesRead.store(999);
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("Loading... 100%") != std::string::npos);

    // FinishLoad clears both the loading state and the progress pointer.
    buffer.FinishLoad(ned::text::Rope("done"));
    REQUIRE(buffer.CurrentLoadProgress() == nullptr);
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("Loading") == std::string::npos);
}

TEST_CASE("ModeLine shows the buffer name and 1-indexed line:column", "[ModeLine]") {
    ned::text::Buffer buffer("myfile.txt", ned::text::Rope("hello\nworld"));
    buffer.SetPoint(8); // line 1 (0-indexed), col 2 (0-indexed) -> displayed L2:C3

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    modeLine.Paint(canvas);

    const std::string row = RowText(screen, 0, 40);
    REQUIRE(row.find("myfile.txt") != std::string::npos);
    REQUIRE(row.find("L2:C3") != std::string::npos);

    // Endpoints of the gradient should match what ned::ui::Color::Interpolate
    // itself produces at t=0/t=1 -- NOT necessarily the theme's raw declared
    // colors bit-for-bit: Interpolate gamma-corrects (pow(x, 2.2) then
    // pow(_, 1/2.2), truncated back to uint8_t), which doesn't always
    // round-trip losslessly at the endpoints for an arbitrary starting RGB
    // value.
    REQUIRE(screen.PixelAt(0, 0).background_color ==
            ned::ui::Color::Interpolate(0.0F, theme.modeLineGradientStart, theme.modeLineGradientEnd));
    REQUIRE(screen.PixelAt(39, 0).background_color ==
            ned::ui::Color::Interpolate(1.0F, theme.modeLineGradientStart, theme.modeLineGradientEnd));
    REQUIRE(screen.PixelAt(0, 0).foreground_color == theme.modeLineForeground);
}

TEST_CASE("ModeLine shows the active mode's name", "[ModeLine]") {
    ned::text::Buffer     buffer("main.c", ned::text::Rope("int main() {}"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::CMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    modeLine.Paint(canvas);

    REQUIRE(RowText(screen, 0, 40).find("(c-mode)") != std::string::npos);
}

TEST_CASE("ModeLine recomputes its text fresh on every paint call", "[ModeLine]") {
    ned::text::Buffer     buffer("scratch", ned::text::Rope("abc"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 40).find("L1:C1") != std::string::npos);

    buffer.SetPoint(3); // end of "abc"
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 40).find("L1:C4") != std::string::npos);
}

TEST_CASE("ModeLine uses the accent-tinted gradient only while its focus provider reports focused", "[ModeLine]") {
    ned::text::Buffer     buffer("scratch", ned::text::Rope("abc"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    // No provider set (every pre-existing construction site): plain gradient.
    modeLine.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).background_color ==
            ned::ui::Color::Interpolate(0.0F, theme.modeLineGradientStart, theme.modeLineGradientEnd));

    bool focused = true;
    modeLine.SetFocusProvider([&focused] { return focused; });
    modeLine.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).background_color ==
            ned::ui::Color::Interpolate(0.0F, theme.modeLineFocusedGradientStart, theme.modeLineFocusedGradientEnd));

    focused = false;
    modeLine.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).background_color ==
            ned::ui::Color::Interpolate(0.0F, theme.modeLineGradientStart, theme.modeLineGradientEnd));
}

TEST_CASE("ModeLine shows a modified marker only when the buffer has unsaved changes", "[ModeLine]") {
    ned::text::Buffer     buffer("scratch", ned::text::Rope("abc"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    REQUIRE_FALSE(buffer.Modified());
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 40).find("*scratch") == std::string::npos);

    buffer.InsertAtPoint("!");
    REQUIRE(buffer.Modified());
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 40).find("*scratch") != std::string::npos);
}

// UTF-8-aware-rendering follow-up: a multi-byte codepoint in a buffer name
// must occupy exactly one cell/column, not one cell per byte -- found live
// via EchoArea's identical bug (see ROADMAP.md) before ModeLine::Paint's own
// byte-per-column loops were converted to AppendUtf8Columns.
TEST_CASE("ModeLine renders a multi-byte UTF-8 buffer name as one cell per codepoint", "[ModeLine]") {
    ned::text::Buffer     buffer("caf\xc3\xa9.txt", ned::text::Rope("x")); // "café.txt"
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    modeLine.Paint(canvas);

    // "  " + modifiedMarker(" ") + "café.txt": c=3 a=4 f=5 é=6 .=7 t=8 x=9 t=10
    REQUIRE(screen.PixelAt(6, 0).character == "\xc3\xa9"); // the whole 'é' in one cell
    REQUIRE(screen.PixelAt(7, 0).character == ".");        // not pushed one column right by a split 'é'
    // Text after the name lands at the COLUMN the codepoint-count (not
    // byte-count) predicts: "  " + " " + "café.txt" (8 codepoints) + "   " = column 14.
    // (RowText concatenates each cell's own UTF-8 bytes into one std::string,
    // so a *byte* offset found via std::string::find would land one byte
    // late here -- checking the actual cell column is what a split 'é' would
    // really shift.)
    REQUIRE(screen.PixelAt(14, 0).character == "L");
    REQUIRE(screen.PixelAt(15, 0).character == "1");
    REQUIRE(screen.PixelAt(16, 0).character == ":");
    REQUIRE(screen.PixelAt(17, 0).character == "C");
    REQUIRE(screen.PixelAt(18, 0).character == "1");
}

TEST_CASE("ModeLine shows an active background activity with its spinner and detail", "[ModeLine]") {
    ned::text::Buffer     buffer("main.c", ned::text::Rope("int main() {}"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::CMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(60, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 0});

    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("LSP") == std::string::npos); // idle -- no activity text

    ned::editor::BeginBackgroundActivity("LSP");
    ned::editor::SetBackgroundActivityDetail("LSP", "indexing (45%)");
    modeLine.Paint(canvas);
    const std::string row = RowText(screen, 0, 60);
    // The spinner frame itself rotates with the wall clock -- assert the
    // stable parts (name, detail) and that a braille frame glyph occupies
    // exactly one cell between them (cells are strings; a multi-byte glyph
    // in one cell makes the row's byte length exceed its column count).
    REQUIRE(row.find("LSP") != std::string::npos);
    REQUIRE(row.find("indexing (45%)") != std::string::npos);
    REQUIRE(row.size() > 60);

    ned::editor::EndBackgroundActivity("LSP");
    modeLine.Paint(canvas);
    // minimum-visible-duration follow-up: ModeLine holds the last non-empty
    // activity snapshot for a short grace window after BackgroundActivity
    // itself reports empty, so a just-ended activity doesn't blink off
    // within a single Paint() call -- see ModeLine.h's own doc comment on
    // lastShownActivities_. Deliberately local to ModeLine's rendering, not
    // BackgroundActivity itself (which keeps reporting empty immediately,
    // unchanged -- see BackgroundActivityTest.cpp), so this still reads
    // "LSP" right after End.
    REQUIRE(RowText(screen, 0, 60).find("LSP") != std::string::npos);

    // Once the grace window has genuinely elapsed (a real sleep -- ModeLine's
    // hold is measured against the wall clock with no test-side injection
    // point), it's gone.
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("LSP") == std::string::npos);
}

TEST_CASE("ModeLine shows a static idle indicator for a running LSP client with no request in flight",
          "[ModeLine]") {
    ned::text::BufferList        bufferList;
    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(bufferList, eventLoop);
    RegisterFakeRunningClient(manager, "c", eventLoop);

    ned::text::Buffer     buffer("main.c", ned::text::Rope("int main() {}"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::CMode(); // LanguageKeyForMode -> "c", matching the client above
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);
    modeLine.SetLspManager(&manager);

    ned::ui::Screen screen = MakeScreen(60, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 0});

    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("LSP") != std::string::npos); // running, even though nothing's in flight

    // A real in-flight activity takes over the same "LSP" entry rather than
    // producing a duplicate.
    ned::editor::BeginBackgroundActivity("LSP");
    modeLine.Paint(canvas);
    const std::string busyRow = RowText(screen, 0, 60);
    REQUIRE(std::count(busyRow.begin(), busyRow.end(), 'P') == 1);
    ned::editor::EndBackgroundActivity("LSP");

    // Back to the idle indicator once the request resolves -- still running,
    // not hidden.
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("LSP") != std::string::npos);
}

TEST_CASE("ModeLine shows no LSP indicator when SetLspManager was never called, or no client runs for this buffer's language",
          "[ModeLine]") {
    ned::text::BufferList        bufferList;
    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(bufferList, eventLoop);
    RegisterFakeRunningClient(manager, "python", eventLoop); // a different language than the buffer below

    ned::text::Buffer     buffer("main.c", ned::text::Rope("int main() {}"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::CMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(60, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 0});

    // SetLspManager never called: no indicator at all, matching every
    // pre-existing construction site's default.
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("LSP") == std::string::npos);

    // Wired, but the only running client is for a different language.
    modeLine.SetLspManager(&manager);
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("LSP") == std::string::npos);
}

TEST_CASE("ModeLine shows a distinct glyph for a spawn failure, not the running dot", "[ModeLine]") {
    ned::text::BufferList        bufferList;
    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(bufferList, eventLoop);
    // LspManagerTest-broker-hermeticity follow-up: without this, ClientForLanguage's
    // real spawn path tries the real broker socket first, and a broker daemon left
    // running from an earlier `ned`/test run makes the expected synchronous spawn
    // failure below flaky -- see LspManagerTest.cpp's matching tests for the full
    // explanation.
    manager.SetBrokerSocketPathOverrideForTesting(std::filesystem::temp_directory_path() / "ned-modeline-test-no-broker.sock");
    ned::editor::lsp::SetLspServerCommand("modeline-spawn-fail-lang", {"/definitely/does/not/exist/ned-fake-lsp"});
    ned::text::Buffer& buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-modeline-spawn-fail-test.txt");
    manager.SyncBuffer(buffer, "modeline-spawn-fail-lang"); // must not throw; latches the failure
    REQUIRE(manager.StatusForLanguage("modeline-spawn-fail-lang") == ned::editor::lsp::LspManager::LspStatus::SpawnFailed);

    ned::ui::ActiveBuffer activeBuffer(buffer);
    // A stand-in mode whose name -- and so LanguageKeyForMode's result, no
    // "-mode" suffix to strip -- matches the language just synced above.
    ned::editor::Mode mode  = ned::editor::FundamentalMode();
    mode.name               = "modeline-spawn-fail-lang";
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::ModeLine modeLine(activeBuffer, mode, theme);
    modeLine.SetLspManager(&manager);

    // Wide enough that the long buffer/mode names used above, plus the
    // spawn-failure detail text (the full "executable not found" message),
    // don't push past the visible column count -- narrower widths are fine
    // for the other tests in this file, which use short buffer/mode names.
    ned::ui::Screen screen = MakeScreen(200, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 199, .y_min = 0, .y_max = 0});

    modeLine.Paint(canvas);
    const std::string row = RowText(screen, 0, 200);
    REQUIRE(row.find("LSP") != std::string::npos);
    REQUIRE(row.find("✕") != std::string::npos);
    REQUIRE(row.find("●") == std::string::npos); // not the running dot
    // mode-line-lsp-status-round-3 follow-up: the spawn-failure detail text
    // (the exception message, which names the failed binary) renders after
    // the glyph.
    REQUIRE(row.find("ned-fake-lsp") != std::string::npos);

    ned::editor::lsp::SetLspServerCommand("modeline-spawn-fail-lang", {}); // clean up global config state for other tests
}

TEST_CASE("ModeLine renders the single-glyph indicator unchanged when only one server is active for the buffer",
          "[ModeLine][EmbeddedDocuments]") {
    // Regression guard for the embedded-language-documents follow-up:
    // ActiveServerKeysForBuffer's size must stay <=1 for an ordinary buffer
    // with no embedded regions, so this renders byte-for-byte identically to
    // "ModeLine shows a static idle indicator..." above.
    ned::text::BufferList        bufferList;
    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(bufferList, eventLoop);
    FakeServer                   server = FakeServer::Create(manager, "c", eventLoop);

    ned::text::Buffer& buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-modeline-single-server-test.c");
    manager.SyncBuffer(buffer, "c");

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::CMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);
    modeLine.SetLspManager(&manager);

    // Wide enough for the full temp-directory buffer path (much longer than
    // the short "main.c" name the pre-existing single-glyph tests use) --
    // same reasoning as the spawn-failure test above.
    ned::ui::Screen screen = MakeScreen(200, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 199, .y_min = 0, .y_max = 0});
    modeLine.Paint(canvas);

    const std::string row = RowText(screen, 0, 200);
    REQUIRE(row.find("LSP") != std::string::npos);
    REQUIRE(row.find("●") != std::string::npos);
    REQUIRE(row.find("c ●") == std::string::npos); // the multi-glyph "<key> <glyph>" shape must not appear here
}

TEST_CASE("ModeLine shows one glyph per active server when more than one is synced for the buffer",
          "[ModeLine][EmbeddedDocuments]") {
    ned::text::BufferList        bufferList;
    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(bufferList, eventLoop);
    FakeServer                   htmlServer = FakeServer::Create(manager, "html", eventLoop);
    FakeServer                   jsServer   = FakeServer::Create(manager, "javascript", eventLoop);

    ned::text::Buffer& buffer =
        bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-modeline-multi-server-test.html");
    manager.SyncBuffer(buffer, "html");
    manager.SyncEmbeddedDocuments(buffer, {ned::editor::lsp::LspManager::EmbeddedDocumentSync{
                                              .language = "javascript", .documentText = "x", .ownedRanges = {{0, 1}}}});

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::HtmlMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);
    modeLine.SetLspManager(&manager);

    ned::ui::Screen screen = MakeScreen(200, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 199, .y_min = 0, .y_max = 0});
    modeLine.Paint(canvas);

    const std::string row = RowText(screen, 0, 200);
    REQUIRE(row.find("html") != std::string::npos);
    REQUIRE(row.find("javascript") != std::string::npos);

    std::size_t dotCount = 0;
    for (std::size_t pos = row.find("\xE2\x97\x8F"); pos != std::string::npos; pos = row.find("\xE2\x97\x8F", pos + 1)) {
        ++dotCount;
    }
    REQUIRE(dotCount == 2); // one running-dot glyph per active server
}

TEST_CASE("ModeLine shows the embedded language at point only while SetLanguageAtPointProvider reports one",
          "[ModeLine][EmbeddedDocuments]") {
    ned::text::Buffer     buffer("index.html", ned::text::Rope("<script></script>"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::HtmlMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(60, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 0});

    // No provider wired yet -- same "safe no-op until wired" default every
    // other Set* hook in this codebase has.
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("[javascript]") == std::string::npos);

    modeLine.SetLanguageAtPointProvider([] { return std::optional<std::string>("javascript"); });
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("[javascript]") != std::string::npos);

    modeLine.SetLanguageAtPointProvider([] { return std::optional<std::string>(); });
    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("[javascript]") == std::string::npos); // back to nothing extra
}

TEST_CASE("ModeLine shows a live clock indicator in org-mode while a clock is running", "[ModeLine]") {
    ned::text::Buffer buffer("notes.org", ned::text::Rope("* Buy milk\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 09:15]\n:END:\n"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::OrgMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(60, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 0});

    modeLine.Paint(canvas);
    // Elapsed time itself is wall-clock-dependent (not asserted here) -- only
    // that the indicator (headline title + its glyph) shows at all.
    const std::string row = RowText(screen, 0, 60);
    REQUIRE(row.find("Buy milk") != std::string::npos);
    REQUIRE(row.find("⏱") != std::string::npos);
}

TEST_CASE("ModeLine shows no clock indicator outside org-mode, even with LOGBOOK-shaped text", "[ModeLine]") {
    ned::text::Buffer buffer("notes.txt", ned::text::Rope("* Buy milk\n:LOGBOOK:\nCLOCK: [2026-08-24 Mon 09:15]\n:END:\n"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::FundamentalMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(60, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 0});

    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("⏱") == std::string::npos);
}

TEST_CASE("ModeLine shows no clock indicator in org-mode when nothing is clocked in", "[ModeLine]") {
    ned::text::Buffer     buffer("notes.org", ned::text::Rope("* Buy milk\n"));
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::editor::Mode     mode  = ned::editor::OrgMode();
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::ModeLine     modeLine(activeBuffer, mode, theme);

    ned::ui::Screen screen = MakeScreen(60, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 0});

    modeLine.Paint(canvas);
    REQUIRE(RowText(screen, 0, 60).find("⏱") == std::string::npos);
}
