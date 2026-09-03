#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <unistd.h>

#include "Editor/BackgroundActivity.h"
#include "Editor/Lsp/LspBackgroundSync.h"
#include "Editor/Lsp/LspClient.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Lsp/LspRootResolver.h"
#include "Editor/Lsp/LspServerConfig.h"
#include "Editor/Lsp/Transport.h"
#include "Editor/ProjectRoot.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

using ned::editor::HighlightSpan;
using ned::editor::ProjectRoot;
using ned::editor::SetProjectRoot;
using ned::editor::SyntaxClass;
using ned::editor::lsp::CodeAction;
using ned::editor::lsp::CompletionItem;
using ned::editor::lsp::Json;
using ned::editor::lsp::kProseLanguageKey;
using ned::editor::lsp::LspClient;
using ned::editor::lsp::LspManager;
using ned::editor::lsp::SemanticTokensLegend;
using ned::editor::lsp::SetLspRootMarkers;
using ned::editor::lsp::TextDocumentSyncKind;
using ned::editor::lsp::Transport;
using ned::text::Buffer;
using ned::text::BufferList;

namespace {

// Mirrors LspClientTest.cpp's own ClientFixture exactly (see that file's
// header comment for the full rationale, including why serverStdoutWrite
// must be closed before the LspClient it feeds) -- a raw pipe pair standing
// in for a real language server's stdin/stdout, used here to drive
// LspManager::SetClientForTesting instead of LspClient directly.
struct FakeServer {
    int serverStdinRead;   // test reads what the client wrote
    int serverStdoutWrite; // test writes to feed the client's (unused, in these tests) read thread

    FakeServer(int readFd, int writeFd) : serverStdinRead(readFd), serverStdoutWrite(writeFd) {
    }

    ~FakeServer() {
        ::close(serverStdoutWrite);
        ::close(serverStdinRead);
    }

    FakeServer(const FakeServer&)            = delete;
    FakeServer& operator=(const FakeServer&) = delete;
    FakeServer(FakeServer&&)                 = default;

    static FakeServer Create(LspManager& manager, const std::string& language, ned::ui::EventLoop& eventLoop, LspClient*& outClient,
                             const Json& workspaceConfiguration = Json::object(), bool brokerBacked = false,
                             std::optional<std::string> connectionKeyOverride = std::nullopt) {
        int clientWritesHere[2]; // client's write end -> test's read end
        int clientReadsHere[2];  // test's write end -> client's read end
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        auto client = std::make_unique<LspClient>(Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop);
        outClient   = &manager.SetClientForTesting(language, std::move(client), workspaceConfiguration, brokerBacked,
                                                   std::move(connectionKeyOverride));
        return FakeServer(clientWritesHere[0], clientReadsHere[1]);
    }
};

// Reads exactly one LSP frame's raw bytes off a plain fd -- copied from
// LspClientTest.cpp's own ReadRawFrame (kept file-local here too rather than
// shared, matching that file's own "not worth a new dependency between the
// two for something this small" precedent elsewhere in this codebase).
std::string ReadRawFrame(int fd) {
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

// graceful-lsp-shutdown follow-up: ReadRawFrame above assumes exactly one
// frame arrives per call, which breaks the moment a caller (Shutdown())
// writes two frames back-to-back before this test ever reads -- both can
// land in the same read() (LspManager::Shutdown's own shutdown+exit pair,
// tiny frames over a fast local pipe), and ReadRawFrame's substr-to-end
// parse would then choke on the second frame's own headers trailing the
// first frame's body. Splits every complete frame out of raw by walking
// Content-Length boundaries instead of assuming there's only one.
std::vector<Json> ParseAllFrames(const std::string& raw) {
    std::vector<Json> frames;
    std::size_t       pos = 0;
    while (true) {
        const std::size_t headerEnd = raw.find("\r\n\r\n", pos);
        if (headerEnd == std::string::npos) {
            break;
        }
        const std::string_view kPrefix   = "Content-Length: ";
        const std::size_t      prefixPos = raw.find(kPrefix, pos);
        if (prefixPos == std::string::npos || prefixPos > headerEnd) {
            break;
        }
        const std::size_t contentLength = std::stoul(raw.substr(prefixPos + kPrefix.size()));
        const std::size_t bodyStart     = headerEnd + 4;
        if (raw.size() < bodyStart + contentLength) {
            break; // frame not fully arrived yet
        }
        frames.push_back(Json::parse(raw.substr(bodyStart, contentLength)));
        pos = bodyStart + contentLength;
    }
    return frames;
}

// Reads until at least frameCount complete frames have arrived (per
// ParseAllFrames above) or the read loop runs dry.
std::string ReadRawFramesUntil(int fd, std::size_t frameCount) {
    std::string all;
    char        buffer[512];
    for (int i = 0; i < 8; ++i) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        all.append(buffer, static_cast<std::size_t>(n));
        if (ParseAllFrames(all).size() >= frameCount) {
            break;
        }
    }
    return all;
}

int RequestIdFromFrame(const std::string& raw) {
    return Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["id"].get<int>();
}

// LSP multi-root follow-up: ProjectRoot() is process-wide state -- mirrors
// ProjectRootTest.cpp's own ProjectRootGuard exactly, so a test that changes
// it restores the value even if a REQUIRE fails partway through.
struct ProjectRootGuard {
    std::filesystem::path previous = ProjectRoot();
    ~ProjectRootGuard() {
        SetProjectRoot(previous);
    }
};

// prose-checking follow-up: asserting "nothing was ever sent" can't use
// ReadRawFrame's own blocking ::read (it would hang forever on a fd that
// legitimately never gets written to -- the case under test). A short,
// bounded poll() is the deliberate exception to this file's otherwise
// blocking-read style, used only here.
bool NoFrameArrives(int fd) {
    pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
    return ::poll(&pfd, 1, 200) == 0; // 0 == timed out, nothing readable
}

// per-frame-sync-materialize follow-up: reads and discards exactly one
// frame, size unbounded -- unlike ReadRawFrame above (capped at four
// 512-byte reads, sized for this file's small fixed JSON payloads), needed
// to drain a real multi-hundred-MiB didOpen concurrently with the send so
// ChildProcess::WriteAll's own stall guard never trips.
void DrainOneFrame(int fd) {
    std::string headerBuf;
    char        chunk[64 * 1024];
    std::size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        const ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n <= 0) {
            return;
        }
        headerBuf.append(chunk, static_cast<std::size_t>(n));
        headerEnd = headerBuf.find("\r\n\r\n");
    }
    const std::string_view kPrefix         = "Content-Length: ";
    const auto             prefixPos       = headerBuf.find(kPrefix);
    const std::size_t      contentLength   = std::stoul(headerBuf.substr(prefixPos + kPrefix.size()));
    const std::size_t      bodyAlreadyRead = headerBuf.size() - (headerEnd + 4);
    std::size_t            remaining       = contentLength > bodyAlreadyRead ? contentLength - bodyAlreadyRead : 0;
    while (remaining > 0) {
        const ssize_t n = ::read(fd, chunk, std::min(sizeof(chunk), remaining));
        if (n <= 0) {
            return;
        }
        remaining -= static_cast<std::size_t>(n);
    }
}

// diagnostics-debounce follow-up: HandlePublishDiagnostics no longer applies
// a publish synchronously -- it (re)arms a per-buffer DeadlineTimer (see
// LspServerConfig.h's LspDiagnosticsDebounceMs) whose fire is Post()ed onto
// eventLoop from a background thread. Polls DrainPosted_ until predicate is
// true or a generous deadline passes, the same real-timer idiom
// PtyProcessTest.cpp's own tests already use.
template <typename Predicate>
void WaitUntil(ned::ui::EventLoop& eventLoop, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        eventLoop.DrainPosted_();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// A plain byte-count check is a sufficient predicate whenever a publish
// changes the total, but not when one message is swapped for another at the
// same count (see "A second publish from one source replaces only that
// source's own diagnostics slice" below, which waits on message content
// instead).
void WaitForDiagnosticCount(ned::ui::EventLoop& eventLoop, const Buffer& buffer, std::size_t expectedCount) {
    WaitUntil(eventLoop, [&] { return buffer.Diagnostics().size() == expectedCount; });
}

} // namespace

TEST_CASE("LspManager::RequestHover resolves synchronously to nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                       invoked = false;
    std::optional<std::string> gotText;
    manager.RequestHover(buffer, 0, [&](std::optional<std::string> text) {
        invoked = true;
        gotText = text;
    });

    REQUIRE(invoked); // no client/pending I/O involved -- fires immediately
    REQUIRE_FALSE(gotText.has_value());
}

TEST_CASE("LspManager::RequestCompletion resolves synchronously to an empty list when the buffer was never synced",
          "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                        invoked = false;
    std::vector<CompletionItem> gotItems;
    manager.RequestCompletion(buffer, 0, [&](std::vector<CompletionItem> items) {
        invoked  = true;
        gotItems = std::move(items);
    });

    REQUIRE(invoked);
    REQUIRE(gotItems.empty());
}

TEST_CASE("LspManager::SyncBuffer is a no-op for a buffer with no associated path", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch"); // no path -- Buffer::Path() == nullopt

    ned::editor::lsp::SetLspServerCommand("test-lang", {"/bin/cat"});
    manager.SyncBuffer(buffer, "test-lang"); // must not spawn anything or crash

    bool invoked = false;
    manager.RequestHover(buffer, 0, [&](std::optional<std::string>) { invoked = true; });
    REQUIRE(invoked); // still resolves synchronously -- SyncBuffer never actually opened it

    ned::editor::lsp::SetLspServerCommand("test-lang", {}); // clean up global config state for other tests
}

TEST_CASE("LspManager::SyncBuffer is a no-op when nothing is configured for the buffer's language", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-test.txt");

    manager.SyncBuffer(buffer, "a-language-nothing-is-configured-for"); // must not crash

    bool invoked = false;
    manager.RequestHover(buffer, 0, [&](std::optional<std::string>) { invoked = true; });
    REQUIRE(invoked);
}

#if defined(__linux__)
// VmRSS in kB, per proc(5) -- same technique PieceTableTest.cpp/
// BufferHugeFileTest.cpp's own [memory] tests use.
std::size_t CurrentRssKb() {
    std::ifstream status("/proc/self/status");
    std::string   line;
    while (std::getline(status, line)) {
        if (line.starts_with("VmRSS:")) {
            return static_cast<std::size_t>(std::stoul(line.substr(line.find_first_of("0123456789"))));
        }
    }
    return 0;
}

// progressive-huge-file-load follow-up: real, reproduced live bug --
// LspManager::SyncToServer used to call buffer.Text() (a full
// Storage_->ToString() materialization) unconditionally, before ever
// checking whether a client is configured for the target language. For a
// huge buffer with no server configured, SyncBackgroundBuffers' periodic
// tick (Source/Editor/Lsp/LspBackgroundSync.cpp) paid that full-document
// copy on every single tick for nothing -- at multi-GB scale this made
// each tick take longer than the tick interval itself, backing up
// EventLoop::Post forever and hanging the whole editor. Fixed by moving
// the ClientForLanguage check ahead of the buffer.Text() argument in
// SyncToServer. This test proves the fix holds: syncing a huge buffer
// against an unconfigured language must not materialize its content.
TEST_CASE("LspManager::SyncBuffer does not materialize a huge buffer's content when no server is configured",
          "[Lsp][memory]") {
    constexpr std::size_t kFileSize = 200 * 1024 * 1024; // 200 MiB -- same "obviously wrong if resident" size the
                                                          // sibling PieceTable/BufferHugeFile [memory] tests use

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_lsp_manager_huge_nosync.txt";
    {
        std::ofstream file(path, std::ios::binary);
        const std::string chunk(1024 * 1024, 'x');
        for (std::size_t written = 0; written < kFileSize; written += chunk.size()) {
            file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        }
    }

    struct ThresholdGuard {
        ~ThresholdGuard() {
            ned::text::SetHugeFileThreshold(1024ull * 1024 * 1024);
        }
    } guard;
    ned::text::SetHugeFileThreshold(4); // well under this file's real size -- forces the huge/PieceTable path

    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenFile(path);
    REQUIRE(buffer.Content().IsHuge());

    const std::size_t rssBeforeKb = CurrentRssKb();
    // Mirrors what SyncBackgroundBuffers actually calls, repeatedly (the
    // real periodic-tick shape) -- must stay cheap every time, not just once.
    for (int i = 0; i < 5; ++i) {
        manager.SyncBuffer(buffer, "a-language-nothing-is-configured-for");
    }
    const std::size_t rssAfterKb = CurrentRssKb();

    const std::size_t growthKb = rssAfterKb > rssBeforeKb ? rssAfterKb - rssBeforeKb : 0;
    REQUIRE(growthKb < kFileSize / 1024 / 4); // < 50 MiB, vs. a 200 MiB file -- a single Text() copy would blow well past this

    std::filesystem::remove(path);
}

// huge-file-lsp-gate follow-up: sibling of the test above, but with a real
// server actually configured/spawned for the buffer's language -- the case
// the earlier ClientForLanguage-ahead-of-buffer.Text() fix did NOT cover,
// since a configured client makes that check pass and fall straight into
// materializing+sending a multi-GB didOpen. Proves SyncBuffer's own
// buffer.Content().IsHuge() gate (checked before ever calling SyncToServer)
// stops that regardless of what's configured, and that no frame reaches
// the server either.
TEST_CASE("LspManager::SyncBuffer does not sync a huge buffer even when a real server is configured",
          "[Lsp][memory]") {
    constexpr std::size_t kFileSize = 200 * 1024 * 1024;

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_lsp_manager_huge_configured.txt";
    {
        std::ofstream     file(path, std::ios::binary);
        const std::string chunk(1024 * 1024, 'x');
        for (std::size_t written = 0; written < kFileSize; written += chunk.size()) {
            file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        }
    }

    struct ThresholdGuard {
        ~ThresholdGuard() {
            ned::text::SetHugeFileThreshold(1024ull * 1024 * 1024);
        }
    } guard;
    ned::text::SetHugeFileThreshold(4); // well under this file's real size -- forces the huge/PieceTable path

    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenFile(path);
    REQUIRE(buffer.Content().IsHuge());

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    const std::size_t rssBeforeKb = CurrentRssKb();
    for (int i = 0; i < 5; ++i) {
        manager.SyncBuffer(buffer, "test-lang");
    }
    const std::size_t rssAfterKb = CurrentRssKb();

    const std::size_t growthKb = rssAfterKb > rssBeforeKb ? rssAfterKb - rssBeforeKb : 0;
    REQUIRE(growthKb < kFileSize / 1024 / 4);
    REQUIRE(NoFrameArrives(server.serverStdinRead)); // a real server was configured, but a huge buffer must never reach it

    std::filesystem::remove(path);
}

// per-frame-sync-materialize follow-up: real, reproduced live bug -- once a
// server IS configured (unlike the two tests above), SyncToServer used to
// build buffer.Text() as an eager function argument on every single call,
// even though SyncTextToServer's own "nothing changed since the last sync"
// check would then immediately turn it into a no-op. BufferView::Paint()
// calls SyncBuffer every frame for the focused buffer, so this ran on every
// repaint forever, not just once -- live-reproduced against a real
// multi-GB file with harper-ls configured as the prose checker (RSS
// oscillating several GB, main thread stalling on every frame). This test
// proves the fix: repeated SyncBuffer calls against an unchanged,
// already-opened buffer must not keep re-materializing its content.
TEST_CASE("LspManager::SyncBuffer does not re-materialize an unchanged buffer's content on repeated calls "
          "once a server is configured",
          "[Lsp][memory]") {
    constexpr std::size_t kFileSize = 200 * 1024 * 1024;

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_lsp_manager_repeated_sync.txt";
    {
        std::ofstream     file(path, std::ios::binary);
        const std::string chunk(1024 * 1024, 'x');
        for (std::size_t written = 0; written < kFileSize; written += chunk.size()) {
            file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        }
    }

    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenFile(path);
    REQUIRE_FALSE(buffer.Content().IsHuge()); // ordinary RopeStorage path -- the bug wasn't specific to PieceTableStorage

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    // The didOpen frame below carries the full 200 MiB document -- far past
    // a pipe's buffer capacity, so it must be drained concurrently with the
    // send or ChildProcess::WriteAll's own hang-protection guard trips
    // (unlike every other test in this file, whose small fixed content
    // always fits in one pipe buffer's worth of slack).
    std::thread drainThread([&] { DrainOneFrame(server.serverStdinRead); });
    manager.SyncBuffer(buffer, "test-lang"); // sends the real didOpen -- gets bufferState_ to "opened"
    drainThread.join();

    const std::size_t rssBeforeKb = CurrentRssKb();
    for (int i = 0; i < 5; ++i) {
        manager.SyncBuffer(buffer, "test-lang"); // content unchanged every time -- must be a cheap no-op
    }
    const std::size_t rssAfterKb = CurrentRssKb();

    const std::size_t growthKb = rssAfterKb > rssBeforeKb ? rssAfterKb - rssBeforeKb : 0;
    REQUIRE(growthKb < kFileSize / 1024 / 4);
    REQUIRE(NoFrameArrives(server.serverStdinRead)); // no didChange should have been sent either

    std::filesystem::remove(path);
}
#endif

TEST_CASE("LspManager::NotifyBufferClosed is a no-op for a buffer that was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    manager.NotifyBufferClosed(buffer); // must not crash
    SUCCEED();
}

TEST_CASE("SyncBuffer's didOpen is sent immediately, never debounced", "[Lsp]") {
    // sync-debounce follow-up: a freshly opened/focused buffer must get
    // diagnostics/highlighting right away -- only the *second+* sync
    // (didChange, after an edit) is debounced. No sleep, no DrainPosted_:
    // if this were debounced too, the frame simply wouldn't be there yet.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-didopen-immediate-test.txt");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");

    const std::string raw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["method"] == "textDocument/didOpen");
}

TEST_CASE("SyncBuffer debounces a rapid burst of edits into a single didChange with the final content", "[Lsp]") {
    // sync-debounce follow-up: the user's own reported bug, made concrete --
    // a burst of edits with no pause between them (well within
    // LspSyncDebounceMs() of each other) must collapse into exactly one
    // textDocument/didChange, not one per keystroke.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-sync-debounce-coalesce-test.txt");
    buffer.InsertAtPoint("a");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    for (const char c : std::string("bcdefghij")) {
        buffer.InsertAtPoint(std::string(1, c));
        manager.SyncBuffer(buffer, "test-lang"); // (re)arms the same debounce timer each time -- no send yet
    }
    // No "nothing sent yet" check here -- NoFrameArrives' own 200ms poll is
    // longer than LspSyncDebounceMs()'s 150ms default, so it would race
    // against the debounce firing mid-poll. WaitUntil below is the real,
    // race-free assertion: exactly one didChange eventually arrives, with
    // the burst's *final* content.
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string didChange = ReadRawFrame(server.serverStdinRead);
    const Json        frame     = Json::parse(didChange.substr(didChange.find("\r\n\r\n") + 4));
    REQUIRE(frame["method"] == "textDocument/didChange");
    REQUIRE(frame["params"]["contentChanges"][0]["text"] == "abcdefghij"); // the *final* content, not an early snapshot
    REQUIRE(NoFrameArrives(server.serverStdinRead));                       // exactly one didChange for the whole burst
}

TEST_CASE("SyncBuffer sends a separate didChange for edits spaced further apart than the debounce", "[Lsp]") {
    // sync-debounce follow-up: the debounce must not merge genuinely
    // separate edits into nothing -- each edit that's allowed to settle
    // still produces its own didChange.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-sync-debounce-separate-test.txt");
    buffer.InsertAtPoint("a");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    buffer.InsertAtPoint("b");
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string firstRaw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(firstRaw.substr(firstRaw.find("\r\n\r\n") + 4))["params"]["contentChanges"][0]["text"] == "ab");

    buffer.InsertAtPoint("c");
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string secondRaw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(secondRaw.substr(secondRaw.find("\r\n\r\n") + 4))["params"]["contentChanges"][0]["text"] == "abc");
}

TEST_CASE("SyncBuffer sends a full-document didChange when the server never advertised textDocumentSync",
          "[Lsp]") {
    // incremental-sync follow-up: regression guard for the default-to-Full
    // decision -- a server this client has never heard a sync-kind
    // capability from must keep getting the exact full-text shape it always
    // has, with no "range"/"rangeLength" keys at all.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(
        std::filesystem::temp_directory_path() / "ned-lsp-manager-incremental-default-full-test.txt");
    buffer.InsertAtPoint("a");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    buffer.InsertAtPoint("b");
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string raw   = ReadRawFrame(server.serverStdinRead);
    const Json        frame = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(frame["params"]["contentChanges"][0]["text"] == "ab");
    REQUIRE_FALSE(frame["params"]["contentChanges"][0].contains("range"));
    REQUIRE_FALSE(frame["params"]["contentChanges"][0].contains("rangeLength"));
}

TEST_CASE("SyncBuffer sends an incremental didChange containing only the changed span for an Incremental-capable "
          "server",
          "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(
        std::filesystem::temp_directory_path() / "ned-lsp-manager-incremental-append-test.txt");
    buffer.InsertAtPoint("abcde");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetTextDocumentSyncKindForTesting("test-lang", TextDocumentSyncKind::Incremental);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    buffer.InsertAtPoint("z"); // point is at end of "abcde" -- appends "z"
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string raw    = ReadRawFrame(server.serverStdinRead);
    const Json        frame  = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    const Json&       change = frame["params"]["contentChanges"][0];
    REQUIRE(change["text"] == "z");
    REQUIRE(change["rangeLength"] == 0);
    REQUIRE(change["range"]["start"]["line"] == 0);
    REQUIRE(change["range"]["start"]["character"] == 5);
    REQUIRE(change["range"]["end"]["line"] == 0);
    REQUIRE(change["range"]["end"]["character"] == 5);
}

TEST_CASE("SyncBuffer's incremental didChange has rangeLength matching the replaced span's UTF-16 length", "[Lsp]") {
    // Uses a non-ASCII replaced character so byte length and UTF-16 length
    // genuinely diverge -- a bug computing rangeLength in bytes instead of
    // UTF-16 units would still pass a plain-ASCII test.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(
        std::filesystem::temp_directory_path() / "ned-lsp-manager-incremental-rangelength-test.txt");
    buffer.InsertAtPoint("caf\xc3\xa9!"); // "café!" -- é is 2 bytes UTF-8, 1 UTF-16 unit

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetTextDocumentSyncKindForTesting("test-lang", TextDocumentSyncKind::Incremental);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    // Replace "é" (byte offset 3, 2 bytes) with "e": delete then insert at the same point.
    buffer.SetPoint(3);
    buffer.DeleteRange(3, 2); // byteOffset, byteLength -- deletes just "é"'s 2 bytes
    buffer.InsertAtPoint("e");
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string raw    = ReadRawFrame(server.serverStdinRead);
    const Json        frame  = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    const Json&       change = frame["params"]["contentChanges"][0];
    REQUIRE(change["text"] == "e");
    REQUIRE(change["rangeLength"] == 1); // "é" is 1 UTF-16 unit, not 2 bytes
}

TEST_CASE("SyncBuffer's incremental diff widens to the outer span across a burst of debounced edits", "[Lsp]") {
    // Mirrors "SyncBuffer debounces a rapid burst of edits..." above, but
    // with Incremental set -- a burst legitimately coalesces into one
    // didChange whose diffed span may widen across the whole burst; what
    // matters is that applying it to the old text reproduces the final
    // content, not that the span is minimal.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(
        std::filesystem::temp_directory_path() / "ned-lsp-manager-incremental-burst-test.txt");
    buffer.InsertAtPoint("a");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetTextDocumentSyncKindForTesting("test-lang", TextDocumentSyncKind::Incremental);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    for (const char c : std::string("bcdefghij")) {
        buffer.InsertAtPoint(std::string(1, c));
        manager.SyncBuffer(buffer, "test-lang");
    }
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string raw    = ReadRawFrame(server.serverStdinRead);
    const Json        frame  = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    const Json&       change = frame["params"]["contentChanges"][0];
    REQUIRE(change.contains("range")); // still incremental, just a wide one
    // Reconstructing "abcdefghij" from the pre-burst text "a" plus this
    // change's own start/end character offsets on the single line confirms
    // the diff is self-consistent, whatever its exact width turned out to be.
    const std::string oldText   = "a";
    const std::size_t startChar = change["range"]["start"]["character"].get<std::size_t>();
    const std::size_t endChar   = change["range"]["end"]["character"].get<std::size_t>();
    const std::string reconstructed =
        oldText.substr(0, startChar) + change["text"].get<std::string>() + oldText.substr(std::min(endChar, oldText.size()));
    REQUIRE(reconstructed == "abcdefghij");
}

TEST_CASE("SyncBuffer falls back to a full-text didChange when textDocumentSync capability is None", "[Lsp]") {
    // Confirms the fallback path is keyed on "!= Incremental", not just
    // "unset" -- an explicit None must also take the full-text path.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(
        std::filesystem::temp_directory_path() / "ned-lsp-manager-incremental-none-fallback-test.txt");
    buffer.InsertAtPoint("a");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetTextDocumentSyncKindForTesting("test-lang", TextDocumentSyncKind::None);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    buffer.InsertAtPoint("b");
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string raw   = ReadRawFrame(server.serverStdinRead);
    const Json        frame = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(frame["params"]["contentChanges"][0]["text"] == "ab");
    REQUIRE_FALSE(frame["params"]["contentChanges"][0].contains("range"));
}

TEST_CASE("NotifyBufferClosed cancels a pending sync debounce cleanly", "[Lsp]") {
    // sync-debounce follow-up: a buffer closed while a debounced didChange
    // is still pending must not crash, and the stale send must never reach
    // the (now-closed, from LspManager's perspective) connection.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-sync-debounce-close-test.txt");
    buffer.InsertAtPoint("a");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    buffer.InsertAtPoint("b");
    manager.SyncBuffer(buffer, "test-lang");    // arms the debounce -- never allowed to fire
    manager.NotifyBufferClosed(buffer);         // must not crash; cancels the pending timer
    (void)ReadRawFrame(server.serverStdinRead); // drain didClose

    // Long enough for the (cancelled) debounce to have fired if it were
    // somehow still live -- nothing should ever arrive.
    std::this_thread::sleep_for(std::chrono::milliseconds(2 * ned::editor::lsp::LspSyncDebounceMs()));
    eventLoop.DrainPosted_();
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("LspManager::RequestHover round-trips a real request/response through an injected client", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-hover-test.txt");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    manager.SyncBuffer(buffer, "test-lang");    // sends didOpen -- gets bufferState_ to "opened"
    (void)ReadRawFrame(server.serverStdinRead); // drain the didOpen notification

    bool                       invoked = false;
    std::optional<std::string> gotText;
    manager.RequestHover(buffer, 0, [&](std::optional<std::string> text) {
        invoked = true;
        gotText = text;
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/hover");
    REQUIRE(request["params"]["position"]["line"] == 0);
    REQUIRE(request["params"]["position"]["character"] == 0);

    const Json response = {{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"result", {{"contents", "it's an int"}}}};
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotText.has_value());
    REQUIRE(*gotText == "it's an int");
}

TEST_CASE("LspManager routes two buffers under different resolved LSP roots to two distinct connections", "[Lsp]") {
    // LSP multi-root follow-up: the actual feature under test -- two buffers
    // whose configured root markers resolve to two different directories
    // (neither the process's own ProjectRoot()) must never share a
    // connection, even though both sync the exact same language/serverKey
    // string. SetClientForTesting's connectionKeyOverride pre-registers a
    // fake server under the exact connection identity SyncBuffer's real
    // resolution path (LspRootResolver.h's ResolveLspRoot, then
    // LspManager's own ConnectionKey) is expected to compute -- see
    // ConnectionKey's own doc comment in LspManager.h for the composition
    // rule asserted here.
    ProjectRootGuard            rootGuard;
    const std::string           language = "lsp-manager-multiroot-test-lang";
    const std::string           marker   = "lsp-manager-multiroot-test.marker";
    const std::filesystem::path base     = std::filesystem::temp_directory_path() / "ned-lsp-manager-multiroot-test";
    const std::filesystem::path pkgA     = base / "packages" / "a";
    const std::filesystem::path pkgB     = base / "packages" / "b";
    std::filesystem::create_directories(pkgA);
    std::filesystem::create_directories(pkgB);
    {
        std::ofstream(pkgA / marker) << "";
    }
    {
        std::ofstream(pkgB / marker) << "";
    }
    SetProjectRoot(base); // deliberately NOT pkgA/pkgB -- neither buffer's resolved root should collapse to this
    SetLspRootMarkers(language, {marker});

    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            bufferA = bufferList.OpenOrCreateFile(pkgA / "file.txt");
    Buffer&            bufferB = bufferList.OpenOrCreateFile(pkgB / "file.txt");

    LspClient* clientA = nullptr;
    LspClient* clientB = nullptr;
    FakeServer serverA = FakeServer::Create(manager, language, eventLoop, clientA, Json::object(), false,
                                            pkgA.string() + '\x1f' + language);
    FakeServer serverB = FakeServer::Create(manager, language, eventLoop, clientB, Json::object(), false,
                                            pkgB.string() + '\x1f' + language);

    manager.SyncBuffer(bufferA, language);
    manager.SyncBuffer(bufferB, language);
    (void)ReadRawFrame(serverA.serverStdinRead); // drain each buffer's own didOpen
    (void)ReadRawFrame(serverB.serverStdinRead);

    bool invokedA = false;
    bool invokedB = false;
    manager.RequestHover(bufferA, 0, [&](std::optional<std::string>) { invokedA = true; });
    manager.RequestHover(bufferB, 0, [&](std::optional<std::string>) { invokedB = true; });

    // Each buffer's own request must reach its own fake server, never the
    // other's -- a shared/collapsed connection would deliver both (or
    // neither) request to a single pipe.
    const std::string rawA = ReadRawFrame(serverA.serverStdinRead);
    const std::string rawB = ReadRawFrame(serverB.serverStdinRead);
    REQUIRE(Json::parse(rawA.substr(rawA.find("\r\n\r\n") + 4))["method"] == "textDocument/hover");
    REQUIRE(Json::parse(rawB.substr(rawB.find("\r\n\r\n") + 4))["method"] == "textDocument/hover");
    REQUIRE(NoFrameArrives(serverA.serverStdinRead)); // bufferB's request never leaked onto serverA's pipe
    REQUIRE(NoFrameArrives(serverB.serverStdinRead)); // and vice versa

    clientA->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(rawA)}, {"result", {{"contents", "a"}}}}.dump());
    clientB->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(rawB)}, {"result", {{"contents", "b"}}}}.dump());
    REQUIRE(invokedA);
    REQUIRE(invokedB);

    SetLspRootMarkers(language, {}); // cleanup -- process-wide state
    std::filesystem::remove_all(base);
}

TEST_CASE("LspManager collapses a buffer's resolved root to the plain server key when it equals ProjectRoot()", "[Lsp]") {
    // The common-case guarantee ConnectionKey's own doc comment makes: a
    // buffer with no configured root markers (or whose nearest marker
    // happens to resolve to editor::ProjectRoot() itself) shares the exact
    // same bare-language-keyed connection every pre-existing single-root
    // test already relies on -- confirmed here by registering under the
    // bare language string (no override) and letting SyncBuffer's real
    // resolution path find it.
    ProjectRootGuard rootGuard;
    SetProjectRoot(std::filesystem::temp_directory_path());

    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer =
        bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-collapse-test.txt");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "lsp-manager-collapse-test-lang", eventLoop, client);

    manager.SyncBuffer(buffer, "lsp-manager-collapse-test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["method"] == "textDocument/didOpen");
}

TEST_CASE("LspManager::ExpireStaleRequests reaches an injected client's own pending request", "[Lsp]") {
    // subprocess-hang-protection follow-up: confirms the manager-level sweep
    // actually forwards to a real running client, not just LspClient's own
    // already-covered unit behavior.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-expire-test.txt");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    bool                       invoked = false;
    std::optional<std::string> gotText;
    manager.RequestHover(buffer, 0, [&](std::optional<std::string> text) {
        invoked = true;
        gotText = text;
    });
    (void)ReadRawFrame(server.serverStdinRead); // drain the hover request itself
    REQUIRE_FALSE(invoked);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    manager.ExpireStaleRequests(std::chrono::milliseconds(1));

    REQUIRE(invoked);
    REQUIRE_FALSE(gotText.has_value()); // synthetic timeout resolves like any other error response
}

TEST_CASE("LspManager::ExpireStaleRequests survives a stale initialize request disconnecting its own client mid-sweep", "[Lsp]") {
    // reentrant-expiry-during-iteration follow-up: confirmed live via a real
    // SIGSEGV (a unique_ptr<LspClient> read back as garbage, inside
    // ExpireStaleRequests itself). A timed-out *initialize* request's
    // synthesized-timeout callback (SpawnClient's own lambda) calls
    // ClientDisconnected on error, which erases the client from clients_
    // synchronously -- and that can happen from inside this very client's
    // own ExpireStaleRequests(maxAge) call, while LspManager::
    // ExpireStaleRequests's loop is still iterating clients_, invalidating
    // the loop's own iterator. "cat" echoes the initialize request's raw
    // bytes straight back -- no "result"/"error" key, so LspClient::
    // DispatchFrame never treats it as a response (see that function's own
    // id-plus-result-or-error check) and the request just stays pending
    // until the timeout below fires.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    manager.SetBrokerSocketPathOverrideForTesting(std::filesystem::temp_directory_path() / "ned-lsp-manager-test-no-broker.sock");
    ned::editor::lsp::SetLspServerCommand("hang-init-lang", {"cat"});

    Buffer& buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-hang-init-test.txt");
    manager.SyncBuffer(buffer, "hang-init-lang"); // spawns cat, sends "initialize", which never validly answers
    REQUIRE(manager.StatusForLanguage("hang-init-lang") == LspManager::LspStatus::Running);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    manager.ExpireStaleRequests(std::chrono::milliseconds(1)); // the crash used to happen here

    WaitUntil(eventLoop, [&] { return manager.StatusForLanguage("hang-init-lang") != LspManager::LspStatus::Running; });
    REQUIRE(manager.StatusForLanguage("hang-init-lang") == LspManager::LspStatus::Disconnected);

    ned::editor::lsp::SetLspServerCommand("hang-init-lang", {}); // clean up global config state for other tests
}

TEST_CASE("LspManager::RequestHover resolves to nullopt on a JSON-RPC error response", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-hover-error-test.txt");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    std::optional<std::string> gotText;
    manager.RequestHover(buffer, 0, [&](std::optional<std::string> text) { gotText = text; });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = {{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"error", {{"code", -32601}, {"message", "nope"}}}};
    client->DispatchFrame(response.dump());

    REQUIRE_FALSE(gotText.has_value());

    // error-visibility follow-up: the error's own "message" is logged, not
    // just silently collapsed into a nullopt result.
    Buffer* log = bufferList.Find(std::string(ned::editor::lsp::kLspLogBufferName));
    REQUIRE(log != nullptr);
    REQUIRE(log->Text().find("test-lang") != std::string::npos);
    REQUIRE(log->Text().find("nope") != std::string::npos);
}

TEST_CASE("LspManager::SyncBuffer reports a spawn failure via *lsp log* instead of throwing, "
          "and doesn't retry every frame",
          "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    // LspManagerTest-broker-hermeticity follow-up: without this, ClientForLanguage's
    // real spawn path tries the *real* broker socket first, and if any broker daemon
    // (this test's own past run, or another `ned` process) is already listening there,
    // the connect succeeds and the expected synchronous spawn failure never happens --
    // it only surfaces later, async, on the broker's own side, after this test's REQUIREs
    // have already run. Point at a path nothing will ever listen on instead.
    manager.SetBrokerSocketPathOverrideForTesting(std::filesystem::temp_directory_path() / "ned-lsp-manager-test-no-broker.sock");
    Buffer& buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-spawn-fail-test.txt");

    ned::editor::lsp::SetLspServerCommand("spawn-fail-lang", {"/definitely/does/not/exist/ned-fake-lsp"});

    manager.SyncBuffer(buffer, "spawn-fail-lang"); // must not throw

    Buffer* log = bufferList.Find(std::string(ned::editor::lsp::kLspLogBufferName));
    REQUIRE(log != nullptr);
    REQUIRE(log->ReadOnly());
    REQUIRE(log->Text().find("spawn-fail-lang") != std::string::npos);
    REQUIRE(log->Text().find("ned-fake-lsp") != std::string::npos);
    const std::size_t lengthAfterFirstFailure = log->Text().size();

    manager.SyncBuffer(buffer, "spawn-fail-lang"); // same command as before -- must not log again
    REQUIRE(bufferList.Find(std::string(ned::editor::lsp::kLspLogBufferName))->Text().size() == lengthAfterFirstFailure);

    // Reconfiguring to a *different* (still-nonexistent) command lifts the
    // gate -- one more attempt, one more log line.
    ned::editor::lsp::SetLspServerCommand("spawn-fail-lang", {"/still/does/not/exist/ned-fake-lsp-2"});
    manager.SyncBuffer(buffer, "spawn-fail-lang");
    REQUIRE(bufferList.Find(std::string(ned::editor::lsp::kLspLogBufferName))->Text().size() > lengthAfterFirstFailure);

    ned::editor::lsp::SetLspServerCommand("spawn-fail-lang", {}); // clean up global config state for other tests
}

TEST_CASE("LspManager::StatusForLanguage reports NotConfigured, Running, and SpawnFailed", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    using ned::editor::lsp::LspManager;
    // See the spawn-failure test above for why this is needed for hermeticity.
    manager.SetBrokerSocketPathOverrideForTesting(std::filesystem::temp_directory_path() / "ned-lsp-manager-test-no-broker.sock");

    REQUIRE(manager.StatusForLanguage("status-test-lang") == LspManager::LspStatus::NotConfigured);
    REQUIRE(manager.SpawnFailureDetail("status-test-lang").empty());
    REQUIRE(manager.DisconnectReason("status-test-lang").empty());

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "status-test-lang", eventLoop, client);
    REQUIRE(manager.StatusForLanguage("status-test-lang") == LspManager::LspStatus::Running);
    REQUIRE(manager.SpawnFailureDetail("status-test-lang").empty());

    ned::editor::lsp::SetLspServerCommand("status-fail-lang", {"/definitely/does/not/exist/ned-fake-lsp"});
    Buffer& buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-status-test.txt");
    manager.SyncBuffer(buffer, "status-fail-lang");
    REQUIRE(manager.StatusForLanguage("status-fail-lang") == LspManager::LspStatus::SpawnFailed);
    REQUIRE(manager.StatusForLanguage("status-test-lang") == LspManager::LspStatus::Running); // unaffected by the other language's failure
    // mode-line-lsp-status-round-3 follow-up: the spawn exception's message
    // is retained for the mode line's detail text.
    REQUIRE(manager.SpawnFailureDetail("status-fail-lang").find("ned-fake-lsp") != std::string::npos);
    REQUIRE(manager.SpawnFailureDetail("status-test-lang").empty()); // unaffected by the other language's failure

    ned::editor::lsp::SetLspServerCommand("status-fail-lang", {}); // clean up global config state for other tests
}

TEST_CASE("LspManager::ClientDisconnected removes the client and updates status on a real disconnect", "[Lsp]") {
    // lsp-use-after-free follow-up: confirmed live -- a real SIGSEGV/ASan
    // heap-use-after-free from LspClient's own background read thread
    // Post()ing a callback that outlived the object. The fix now lives in
    // LspClient itself (alive_, see LspClient.h's own header comment and
    // LspClientTest.cpp's "A stray Post()ed callback safely no-ops..." for
    // the test that actually exercises that race) rather than here --
    // ClientDisconnected went back to a plain, immediate clients_.erase()
    // once that was fixed at the source. An earlier version of this fix
    // tried deferring destruction here instead (a retired_ vector, drained
    // by a periodic tick) and was confirmed live to not actually be safe at
    // any delay -- LspClient's own periodic maintenance tick and a client's
    // background thread both Post() against EventLoop with no ordering
    // guarantee between them. This test just confirms the ordinary,
    // expected behavior: a real disconnect removes the client and updates
    // status, full stop.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    LspClient* client = nullptr;
    auto       server = std::make_optional<FakeServer>(FakeServer::Create(manager, "disconnect-test-lang", eventLoop, client));
    REQUIRE(client != nullptr);
    REQUIRE(manager.StatusForLanguage("disconnect-test-lang") == LspManager::LspStatus::Running);

    server.reset(); // closes the fake server's write end -- EOF, the real disconnect path
    WaitUntil(eventLoop, [&] { return manager.StatusForLanguage("disconnect-test-lang") != LspManager::LspStatus::Running; });
    REQUIRE(manager.StatusForLanguage("disconnect-test-lang") == LspManager::LspStatus::Disconnected);
}

TEST_CASE("ClientDisconnected erases the cached textDocumentSync capability, re-defaulting to Full", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    LspClient* client = nullptr;
    auto       server = std::make_optional<FakeServer>(FakeServer::Create(manager, "disconnect-sync-kind-test-lang", eventLoop, client));
    REQUIRE(client != nullptr);
    manager.SetTextDocumentSyncKindForTesting("disconnect-sync-kind-test-lang", TextDocumentSyncKind::Incremental);
    REQUIRE(manager.TextDocumentSyncKindFor("disconnect-sync-kind-test-lang") == TextDocumentSyncKind::Incremental);

    server.reset(); // closes the fake server's write end -- EOF, the real disconnect path
    WaitUntil(eventLoop, [&] {
        return manager.StatusForLanguage("disconnect-sync-kind-test-lang") != LspManager::LspStatus::Running;
    });
    REQUIRE(manager.TextDocumentSyncKindFor("disconnect-sync-kind-test-lang") == TextDocumentSyncKind::Full);
}

TEST_CASE("LspManager::ClientDisconnected gives up after a burst of immediate disconnects (crash-loop guard)", "[Lsp]") {
    // crash-loop-respawn-guard follow-up: confirmed live -- a misconfigured
    // phpantom_lsp respawned thousands of times within about a second, since
    // nothing previously stood between one ClientDisconnected and the very
    // next SyncBuffer's respawn attempt. Simulates the same rapid-disconnect
    // shape (inject a client, immediately EOF it, repeat) without a real
    // subprocess at all.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    ned::editor::lsp::SetLspServerCommand("crashloop-lang", {"/definitely/does/not/exist/ned-crashloop-lsp"});

    for (int i = 0; i < 3; ++i) {
        LspClient* client = nullptr;
        {
            FakeServer server = FakeServer::Create(manager, "crashloop-lang", eventLoop, client);
            // FakeServer's destructor closes serverStdoutWrite here -- EOF,
            // which LspClient's own read loop reports as onDisconnected_.
        }
        WaitUntil(eventLoop, [&] { return manager.StatusForLanguage("crashloop-lang") != LspManager::LspStatus::Running; });
    }

    REQUIRE(manager.StatusForLanguage("crashloop-lang") == LspManager::LspStatus::SpawnFailed);
    REQUIRE(manager.SpawnFailureDetail("crashloop-lang").find("disconnects in a row") != std::string::npos);

    ned::editor::lsp::SetLspServerCommand("crashloop-lang", {}); // clean up global config state for other tests
}

TEST_CASE("LspManager::RequestCompletion round-trips a real request/response through an injected client", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-completion-test.txt");
    buffer.InsertAtPoint("foo");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    bool                        invoked = false;
    std::vector<CompletionItem> gotItems;
    manager.RequestCompletion(buffer, buffer.Point(), [&](std::vector<CompletionItem> items) {
        invoked  = true;
        gotItems = std::move(items);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/completion");
    REQUIRE(request["params"]["position"]["character"] == 3);
    // completion-context follow-up: every caller is a manual/explicit
    // trigger, never a specific tracked trigger character.
    REQUIRE(request["params"]["context"]["triggerKind"] == 1);

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", {{"isIncomplete", false}, {"items", Json::array({{{"label", "foobar"}, {"insertText", "foobar"}}})}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotItems.size() == 1);
    REQUIRE(gotItems[0].label == "foobar");
}

TEST_CASE("LspManager routes a real publishDiagnostics notification into Buffer::Diagnostics()", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-diagnostics-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    const Json notification = {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params",
         {{"uri", "file://" + path.string()},
          {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                        {"severity", 1},
                                        {"message", "syntax error"}}})}}},
    };
    client->DispatchFrame(notification.dump());
    WaitForDiagnosticCount(eventLoop, buffer, 1);

    REQUIRE(buffer.Diagnostics().size() == 1);
    REQUIRE(buffer.Diagnostics()[0].message == "syntax error");
    REQUIRE(buffer.Diagnostics()[0].severity == Buffer::Diagnostic::Severity::Error);
}

TEST_CASE("A publishDiagnostics notification is not applied until the debounce delay elapses", "[Lsp]") {
    const int originalDebounceMs = ned::editor::lsp::LspDiagnosticsDebounceMs();
    ned::editor::lsp::SetLspDiagnosticsDebounceMs(100);

    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-diagnostics-debounce-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    const Json notification = {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params",
         {{"uri", "file://" + path.string()},
          {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                        {"severity", 1},
                                        {"message", "syntax error"}}})}}},
    };
    client->DispatchFrame(notification.dump());
    eventLoop.DrainPosted_();
    REQUIRE(buffer.Diagnostics().empty()); // still pending -- the debounce delay hasn't elapsed yet

    WaitForDiagnosticCount(eventLoop, buffer, 1);
    REQUIRE(buffer.Diagnostics().size() == 1);

    ned::editor::lsp::SetLspDiagnosticsDebounceMs(originalDebounceMs);
}

TEST_CASE("A rapid burst of publishes collapses into a single application using only the latest content", "[Lsp]") {
    const int originalDebounceMs = ned::editor::lsp::LspDiagnosticsDebounceMs();
    ned::editor::lsp::SetLspDiagnosticsDebounceMs(150);

    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-diagnostics-burst-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code bad code bad code");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    auto notificationFor = [&](const std::string& message) {
        return Json{
            {"jsonrpc", "2.0"},
            {"method", "textDocument/publishDiagnostics"},
            {"params",
             {{"uri", "file://" + path.string()},
              {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                            {"severity", 1},
                                            {"message", message}}})}}},
        };
    };

    // Three publishes in quick succession -- each one rearms the same
    // buffer-level debounce timer, so only the last should ever reach the
    // buffer, and only once, well after the burst.
    client->DispatchFrame(notificationFor("first pass").dump());
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    client->DispatchFrame(notificationFor("second pass").dump());
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    client->DispatchFrame(notificationFor("final pass").dump());
    eventLoop.DrainPosted_();
    REQUIRE(buffer.Diagnostics().empty()); // still coalescing -- none of the three has landed yet

    WaitUntil(eventLoop, [&] { return !buffer.Diagnostics().empty(); });
    REQUIRE(buffer.Diagnostics().size() == 1);
    REQUIRE(buffer.Diagnostics()[0].message == "final pass");

    ned::editor::lsp::SetLspDiagnosticsDebounceMs(originalDebounceMs);
}

TEST_CASE("LspManager::RequestCodeActions sends the range and overlapping diagnostics, and round-trips a real response",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-code-action-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code");
    buffer.SetDiagnostics({
        Buffer::Diagnostic{.startByte = 0, .endByte = 3, .severity = Buffer::Diagnostic::Severity::Error, .message = "boom"},
    });

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                    invoked = false;
    std::vector<CodeAction> gotActions;
    manager.RequestCodeActions(buffer, 0, 3, [&](std::vector<CodeAction> actions) {
        invoked    = true;
        gotActions = std::move(actions);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/codeAction");
    REQUIRE(request["params"]["range"]["start"]["character"] == 0);
    REQUIRE(request["params"]["range"]["end"]["character"] == 3);
    REQUIRE(request["params"]["context"]["diagnostics"].size() == 1);
    REQUIRE(request["params"]["context"]["diagnostics"][0]["message"] == "boom");
    REQUIRE(request["params"]["context"]["diagnostics"][0]["severity"] == 1);

    const std::string ownUri   = request["params"]["textDocument"]["uri"].get<std::string>();
    const Json        response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"title", "Fix the boom"},
                                 {"edit",
                                  {{"changes",
                                    {{ownUri, Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                                            {"newText", "good"}}})}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotActions.size() == 1);
    REQUIRE(gotActions[0].title == "Fix the boom");
    REQUIRE(gotActions[0].hasEdit);
    REQUIRE_FALSE(gotActions[0].touchesUnsupportedForm);
    REQUIRE(gotActions[0].edits.size() == 1);
    REQUIRE(gotActions[0].edits[0].edits.size() == 1);
    REQUIRE(gotActions[0].edits[0].edits[0].newText == "good");
}

TEST_CASE("LspManager::RequestCodeActions resolves to an empty list when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                    invoked = false;
    std::vector<CodeAction> gotActions;
    manager.RequestCodeActions(buffer, 0, 0, [&](std::vector<CodeAction> actions) {
        invoked    = true;
        gotActions = std::move(actions);
    });

    REQUIRE(invoked);
    REQUIRE(gotActions.empty());
}

TEST_CASE("LspManager::ResolveCodeAction sends action.raw verbatim and returns the resolved edit", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-resolve-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    const std::string didOpen = ReadRawFrame(server.serverStdinRead);
    const std::string ownUri  = Json::parse(didOpen.substr(didOpen.find("\r\n\r\n") + 4))["params"]["textDocument"]["uri"].get<std::string>();

    // A resolvable action (as ExtractCodeActions would have produced it):
    // "kind" present, no "edit" yet.
    const Json                         unresolved = {{"title", "Remove unused #include"}, {"kind", "quickfix"}, {"data", {{"opaque", 7}}}};
    const ned::editor::lsp::CodeAction action     = ned::editor::lsp::ExtractSingleCodeAction(unresolved, ownUri);
    REQUIRE(action.resolvable);

    bool                                        invoked = false;
    std::optional<ned::editor::lsp::CodeAction> gotResolved;
    manager.ResolveCodeAction(buffer, action, [&](std::optional<ned::editor::lsp::CodeAction> resolved) {
        invoked     = true;
        gotResolved = std::move(resolved);
    });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        sentBack = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["params"];
    REQUIRE(sentBack == unresolved); // the exact original item, round-tripped verbatim

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", {{"title", "Remove unused #include"}, {"kind", "quickfix"}, {"edit", {{"changes", {{ownUri, Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 8}}}}}, {"newText", ""}}})}}}}}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotResolved.has_value());
    REQUIRE(gotResolved->hasEdit);
    REQUIRE(gotResolved->edits.size() == 1);
}

TEST_CASE("LspManager::ResolveCodeActionEdits resolves a URI per touched file", "[Lsp]") {
    CodeAction action;
    action.hasEdit = true;
    action.edits   = {
        ned::editor::lsp::RenameEdit{
            .uri   = "file:///a.c",
            .edits = {ned::editor::lsp::WorkspaceTextEdit{.newText = "x"}},
        },
        ned::editor::lsp::RenameEdit{
            .uri   = "file:///b.c",
            .edits = {ned::editor::lsp::WorkspaceTextEdit{.newText = "y"}},
        },
    };

    const auto resolved = LspManager::ResolveCodeActionEdits(action);
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->size() == 2);
    REQUIRE((*resolved)[0].path == std::filesystem::path("/a.c"));
    REQUIRE((*resolved)[0].edits.size() == 1);
    REQUIRE((*resolved)[0].edits[0].newText == "x");
    REQUIRE((*resolved)[1].path == std::filesystem::path("/b.c"));
}

TEST_CASE("LspManager::ResolveCodeActionEdits returns nullopt for touchesUnsupportedForm or a missing edit", "[Lsp]") {
    CodeAction unsupported;
    unsupported.hasEdit                = true;
    unsupported.touchesUnsupportedForm = true;
    REQUIRE_FALSE(LspManager::ResolveCodeActionEdits(unsupported).has_value());

    CodeAction noEdit;
    noEdit.hasEdit = false;
    REQUIRE_FALSE(LspManager::ResolveCodeActionEdits(noEdit).has_value());
}

TEST_CASE("LspManager::ResolveCodeActionEdits refuses wholesale when one URI doesn't resolve", "[Lsp]") {
    CodeAction action;
    action.hasEdit = true;
    action.edits   = {
        ned::editor::lsp::RenameEdit{.uri = "file:///a.c", .edits = {ned::editor::lsp::WorkspaceTextEdit{.newText = "x"}}},
        ned::editor::lsp::RenameEdit{.uri = "not-a-file-uri", .edits = {ned::editor::lsp::WorkspaceTextEdit{.newText = "y"}}},
    };

    REQUIRE_FALSE(LspManager::ResolveCodeActionEdits(action).has_value());
}

TEST_CASE("LspManager::ResolveCodeAction resolves to nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    ned::editor::lsp::CodeAction action;
    action.title      = "Fix";
    action.resolvable = true;
    action.raw        = Json{{"title", "Fix"}, {"kind", "quickfix"}};

    bool                                        invoked = false;
    std::optional<ned::editor::lsp::CodeAction> gotResolved;
    manager.ResolveCodeAction(buffer, action, [&](std::optional<ned::editor::lsp::CodeAction> resolved) {
        invoked     = true;
        gotResolved = resolved;
    });

    REQUIRE(invoked);
    REQUIRE_FALSE(gotResolved.has_value());
}

TEST_CASE("LspManager::RequestCodeActions with a serverKey routes to that connection, not the primary one", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-code-action-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("teh");

    LspClient* primaryClient = nullptr;
    LspClient* proseClient   = nullptr;
    FakeServer primaryServer = FakeServer::Create(manager, "test-lang", eventLoop, primaryClient);
    FakeServer proseServer   = FakeServer::Create(manager, std::string(kProseLanguageKey), eventLoop, proseClient);

    manager.SyncBuffer(buffer, "test-lang"); // syncs both the primary language server and the prose connection
    (void)ReadRawFrame(primaryServer.serverStdinRead);
    (void)ReadRawFrame(proseServer.serverStdinRead);

    bool                    invoked = false;
    std::vector<CodeAction> gotActions;
    manager.RequestCodeActions(
        buffer, 0, 3, [&](std::vector<CodeAction> actions) {
            invoked    = true;
            gotActions = std::move(actions);
        },
        std::string(kProseLanguageKey));

    REQUIRE(NoFrameArrives(primaryServer.serverStdinRead)); // never asked the primary language server

    const std::string raw     = ReadRawFrame(proseServer.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/codeAction");

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"title", "Add to dictionary"}, {"command", "HarperAddToUserDict"}, {"arguments", Json::array()}}})},
    };
    proseClient->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotActions.size() == 1);
    REQUIRE(gotActions[0].title == "Add to dictionary");
    REQUIRE(gotActions[0].command.has_value());
    REQUIRE(gotActions[0].command->name == "HarperAddToUserDict");
}

TEST_CASE("LspManager::ExecuteCommand sends workspace/executeCommand and reports ok on a real response", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-execute-command-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool invoked = false;
    bool gotOk   = false;
    manager.ExecuteCommand(buffer, {}, "HarperAddToUserDict", Json::array({"teh"}), [&](bool ok) {
        invoked = true;
        gotOk   = ok;
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "workspace/executeCommand");
    REQUIRE(request["params"]["command"] == "HarperAddToUserDict");
    REQUIRE(request["params"]["arguments"] == Json::array({"teh"}));

    const Json response = {{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"result", Json(nullptr)}};
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotOk);
}

TEST_CASE("LspManager::ExecuteCommand reports failure on a JSON-RPC error response", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-execute-command-error-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    bool gotOk = true;
    manager.ExecuteCommand(buffer, {}, "unknown.command", Json::array(), [&](bool ok) { gotOk = ok; });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = {{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"error", {{"code", -32601}, {"message", "unknown command"}}}};
    client->DispatchFrame(response.dump());

    REQUIRE_FALSE(gotOk);
}

TEST_CASE("LspManager::ExecuteCommand reports failure when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool invoked = false;
    bool gotOk   = true;
    manager.ExecuteCommand(buffer, {}, "whatever", Json::array(), [&](bool ok) {
        invoked = true;
        gotOk   = ok;
    });

    REQUIRE(invoked);
    REQUIRE_FALSE(gotOk);
}

TEST_CASE("LspManager::RequestDefinition resolves a Location-array response's uris to real paths", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-definition-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                      invoked = false;
    std::vector<LspManager::ResolvedLocation> got;
    manager.RequestDefinition(buffer, 0, [&](std::vector<LspManager::ResolvedLocation> locations) {
        invoked = true;
        got     = std::move(locations);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/definition");

    const std::filesystem::path definitionPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-definition-target.txt";
    const Json                  response       = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"uri", "file://" + definitionPath.string()},
                                 {"range", {{"start", {{"line", 3}, {"character", 7}}}, {"end", {{"line", 3}, {"character", 11}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].path == definitionPath);
    REQUIRE(got[0].position.line == 3);
    REQUIRE(got[0].position.character == 7);
}

TEST_CASE("LspManager::RequestDefinition resolves to an empty list when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                      invoked = false;
    std::vector<LspManager::ResolvedLocation> got;
    manager.RequestDefinition(buffer, 0, [&](std::vector<LspManager::ResolvedLocation> locations) {
        invoked = true;
        got     = std::move(locations);
    });

    REQUIRE(invoked);
    REQUIRE(got.empty());
}

// declaration/typeDefinition/implementation follow-up: one representative
// test per sibling request, confirming each sends its own distinct wire
// method and still resolves a response through the shared
// ExtractDefinitionLocations/uri-to-path path RequestDefinition's own test
// above already covers in full -- no need to re-test empty-result/unsynced
// cases three more times, that logic is shared (SendLocationRequest), not
// duplicated per method.
TEST_CASE("LspManager::RequestDeclaration sends textDocument/declaration and resolves a response", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-declaration-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                      invoked = false;
    std::vector<LspManager::ResolvedLocation> got;
    manager.RequestDeclaration(buffer, 0, [&](std::vector<LspManager::ResolvedLocation> locations) {
        invoked = true;
        got     = std::move(locations);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/declaration");

    const std::filesystem::path targetPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-declaration-target.txt";
    const Json                  response   = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", {{"uri", "file://" + targetPath.string()}, {"range", {{"start", {{"line", 1}, {"character", 0}}}, {"end", {{"line", 1}, {"character", 4}}}}}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].path == targetPath);
}

TEST_CASE("LspManager::RequestTypeDefinition sends textDocument/typeDefinition", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-typedefinition-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    manager.RequestTypeDefinition(buffer, 0, [](std::vector<LspManager::ResolvedLocation>) {});

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/typeDefinition");
}

TEST_CASE("LspManager::RequestImplementation sends textDocument/implementation", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-implementation-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    manager.RequestImplementation(buffer, 0, [](std::vector<LspManager::ResolvedLocation>) {});

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/implementation");
}

// find-references follow-up: same "sends its own distinct wire method"
// shape as the three tests above, plus the one thing that's actually unique
// to this request -- a "context": {"includeDeclaration": true} field none
// of the other three location requests send.
TEST_CASE("LspManager::RequestReferences sends textDocument/references with includeDeclaration and resolves a response",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-references-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                      invoked = false;
    std::vector<LspManager::ResolvedLocation> got;
    manager.RequestReferences(buffer, 0, [&](std::vector<LspManager::ResolvedLocation> locations) {
        invoked = true;
        got     = std::move(locations);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/references");
    REQUIRE(request["params"]["context"]["includeDeclaration"] == true);

    const std::filesystem::path targetPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-references-target.txt";
    const Json                  response   = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"uri", "file://" + targetPath.string()},
                                 {"range", {{"start", {{"line", 2}, {"character", 3}}}, {"end", {{"line", 2}, {"character", 7}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].path == targetPath);
    REQUIRE(got[0].position.line == 2);
}

// symbol-search follow-up.
TEST_CASE("LspManager::RequestDocumentSymbols sends textDocument/documentSymbol and resolves its own uri to a path",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-docsymbol-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("struct Widget {};");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                  invoked = false;
    std::vector<LspManager::SymbolResult> got;
    manager.RequestDocumentSymbols(buffer, [&](std::vector<LspManager::SymbolResult> symbols) {
        invoked = true;
        got     = std::move(symbols);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/documentSymbol");
    REQUIRE(request["params"].contains("textDocument"));
    REQUIRE_FALSE(request["params"].contains("position")); // no position for this request, unlike hover/definition/etc.

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"name", "Widget"},
                                 {"kind", 23},
                                 {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 18}}}}},
                                 {"selectionRange",
                                  {{"start", {{"line", 0}, {"character", 7}}}, {"end", {{"line", 0}, {"character", 13}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].name == "Widget");
    REQUIRE(got[0].kind == 23);
    REQUIRE(got[0].path == path);
    REQUIRE(got[0].position.character == 7);
}

TEST_CASE("LspManager::RequestWorkspaceSymbols sends workspace/symbol with the query and no textDocument field",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-wssymbol-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("x");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    bool                                  invoked = false;
    std::vector<LspManager::SymbolResult> got;
    manager.RequestWorkspaceSymbols(buffer, "Wid", [&](std::vector<LspManager::SymbolResult> symbols) {
        invoked = true;
        got     = std::move(symbols);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "workspace/symbol");
    REQUIRE(request["params"]["query"] == "Wid");
    REQUIRE_FALSE(request["params"].contains("textDocument"));

    const std::filesystem::path resultPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-wssymbol-result-test.cpp";
    const Json                  response   = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"name", "Widget"},
                                 {"kind", 5},
                                 {"containerName", "ui"},
                                 {"location",
                                  {{"uri", "file://" + resultPath.string()},
                                   {"range", {{"start", {{"line", 4}, {"character", 0}}}, {"end", {{"line", 4}, {"character", 6}}}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].name == "Widget");
    REQUIRE(got[0].containerName == "ui");
    REQUIRE(got[0].path == resultPath);
    REQUIRE(got[0].position.line == 4);
}

TEST_CASE("LspManager::RequestPrepareCallHierarchy sends textDocument/prepareCallHierarchy and resolves the item's uri "
          "to a path",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prepare-callh-test.cpp";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    const std::string didOpen = ReadRawFrame(server.serverStdinRead);
    const std::string ownUri  = Json::parse(didOpen.substr(didOpen.find("\r\n\r\n") + 4))["params"]["textDocument"]["uri"].get<std::string>();

    bool                                              invoked = false;
    std::vector<LspManager::ResolvedHierarchyItem> got;
    manager.RequestPrepareCallHierarchy(buffer, 0, [&](std::vector<LspManager::ResolvedHierarchyItem> items) {
        invoked = true;
        got     = std::move(items);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/prepareCallHierarchy");
    REQUIRE(request["params"]["textDocument"]["uri"] == ownUri);
    REQUIRE(request["params"]["position"]["line"] == 0);

    const Json itemJson = {
        {"name", "call_site"},       {"kind", 12},
        {"uri", ownUri},             {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 9}}}}},
        {"selectionRange", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 9}}}}},
        {"data", {{"opaque", 1}}},
    };
    const Json response = {{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"result", Json::array({itemJson})}};
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].item.name == "call_site");
    REQUIRE(got[0].path == path);
    REQUIRE(got[0].item.raw == itemJson); // full item kept verbatim, not just name/kind/position
}

TEST_CASE("LspManager::RequestIncomingCalls sends item.raw verbatim as \"item\" and resolves fromRanges/from.uri",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-incoming-calls-test.cpp";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("callee();");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    const std::string didOpen = ReadRawFrame(server.serverStdinRead);
    const std::string ownUri  = Json::parse(didOpen.substr(didOpen.find("\r\n\r\n") + 4))["params"]["textDocument"]["uri"].get<std::string>();

    const Json                  requestedItem = {{"name", "callee"},
                                                 {"kind", 12},
                                                 {"uri", ownUri},
                                                 {"selectionRange", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 6}}}}},
                                                 {"data", {{"opaque", 2}}}};
    const ned::editor::lsp::HierarchyItem item = ned::editor::lsp::ExtractHierarchyItems(Json::array({requestedItem}))[0];

    bool                                              invoked = false;
    std::vector<LspManager::ResolvedHierarchyCall> got;
    manager.RequestIncomingCalls(buffer, item, [&](std::vector<LspManager::ResolvedHierarchyCall> calls) {
        invoked = true;
        got     = std::move(calls);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "callHierarchy/incomingCalls");
    REQUIRE(request["params"]["item"] == requestedItem); // round-tripped verbatim, including "data"

    const std::filesystem::path callerPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-caller-test.cpp";
    const Json                  callerItem = {
        {"name", "caller"},
        {"kind", 12},
        {"uri", "file://" + callerPath.string()},
        {"selectionRange", {{"start", {{"line", 3}, {"character", 0}}}, {"end", {{"line", 3}, {"character", 6}}}}},
    };
    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"from", callerItem},
                                 {"fromRanges", Json::array({{{"start", {{"line", 5}, {"character", 2}}}, {"end", {{"line", 5}, {"character", 8}}}}})}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].item.item.name == "caller");
    REQUIRE(got[0].item.path == callerPath);
    REQUIRE(got[0].callSites.size() == 1);
    REQUIRE(got[0].callSites[0].line == 5);
}

TEST_CASE("LspManager::RequestSupertypes sends typeHierarchy/supertypes with item.raw and resolves the response",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-supertypes-test.cpp";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("class Derived {};");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    const std::string didOpen = ReadRawFrame(server.serverStdinRead);
    const std::string ownUri  = Json::parse(didOpen.substr(didOpen.find("\r\n\r\n") + 4))["params"]["textDocument"]["uri"].get<std::string>();

    const Json                             requestedItem = {{"name", "Derived"},
                                                 {"kind", 5},
                                                 {"uri", ownUri},
                                                 {"selectionRange", {{"start", {{"line", 0}, {"character", 6}}}, {"end", {{"line", 0}, {"character", 13}}}}}};
    const ned::editor::lsp::HierarchyItem item          = ned::editor::lsp::ExtractHierarchyItems(Json::array({requestedItem}))[0];

    bool                                              invoked = false;
    std::vector<LspManager::ResolvedHierarchyItem> got;
    manager.RequestSupertypes(buffer, item, [&](std::vector<LspManager::ResolvedHierarchyItem> items) {
        invoked = true;
        got     = std::move(items);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "typeHierarchy/supertypes");
    REQUIRE(request["params"]["item"] == requestedItem);

    const Json baseItem = {{"name", "Base"},
                           {"kind", 5},
                           {"uri", ownUri},
                           {"selectionRange", {{"start", {{"line", 4}, {"character", 6}}}, {"end", {{"line", 4}, {"character", 10}}}}}};
    client->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"result", Json::array({baseItem})}}.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].item.name == "Base");
    REQUIRE(got[0].path == path);
}

TEST_CASE("LspManager::RequestPrepareCallHierarchy resolves an empty vector when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                              invoked = false;
    std::vector<LspManager::ResolvedHierarchyItem> got{LspManager::ResolvedHierarchyItem{}}; // pre-seeded, must be cleared
    manager.RequestPrepareCallHierarchy(buffer, 0, [&](std::vector<LspManager::ResolvedHierarchyItem> items) {
        invoked = true;
        got     = std::move(items);
    });

    REQUIRE(invoked);
    REQUIRE(got.empty());
}

TEST_CASE("LspManager::RequestSignatureHelp sends textDocument/signatureHelp and resolves the formatted text", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-signature-help-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("foo(");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                       invoked = false;
    std::optional<std::string> got;
    manager.RequestSignatureHelp(buffer, buffer.Point(), [&](std::optional<std::string> text) {
        invoked = true;
        got     = std::move(text);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/signatureHelp");

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", {{"signatures", Json::array({{{"label", "foo(a: int)"}, {"parameters", Json::array({{{"label", "a: int"}}})}}})}, {"activeParameter", 0}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.has_value());
    REQUIRE(*got == "foo(**a: int**)");
}

TEST_CASE("LspManager::RequestSignatureHelp resolves nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                       invoked = false;
    std::optional<std::string> got;
    manager.RequestSignatureHelp(buffer, 0, [&](std::optional<std::string> text) {
        invoked = true;
        got     = std::move(text);
    });

    REQUIRE(invoked);
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("LspManager::RequestDocumentHighlight sends textDocument/documentHighlight and resolves the parsed ranges", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-document-highlight-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("foo = foo + 1");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                          invoked = false;
    std::vector<ned::editor::lsp::DocumentHighlight> got;
    manager.RequestDocumentHighlight(buffer, buffer.Point(), [&](std::vector<ned::editor::lsp::DocumentHighlight> highlights) {
        invoked = true;
        got     = std::move(highlights);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/documentHighlight");

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}}, {"kind", 3}},
                                {{"range", {{"start", {{"line", 0}, {"character", 7}}}, {"end", {{"line", 0}, {"character", 10}}}}}, {"kind", 2}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 2);
    REQUIRE(got[0].kind == 3);
    REQUIRE(got[1].start.character == 7);
}

TEST_CASE("LspManager::RequestDocumentHighlight resolves an empty vector when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                          invoked = false;
    std::vector<ned::editor::lsp::DocumentHighlight> got;
    manager.RequestDocumentHighlight(buffer, 0, [&](std::vector<ned::editor::lsp::DocumentHighlight> highlights) {
        invoked = true;
        got     = std::move(highlights);
    });

    REQUIRE(invoked);
    REQUIRE(got.empty());
}

TEST_CASE("LspManager::RequestFormatting sends textDocument/formatting with tabSize/insertSpaces and resolves edits", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-formatting-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x=1;");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                                        invoked = false;
    std::optional<std::vector<ned::editor::lsp::WorkspaceTextEdit>> got;
    manager.RequestFormatting(buffer, [&](std::optional<std::vector<ned::editor::lsp::WorkspaceTextEdit>> edits) {
        invoked = true;
        got     = std::move(edits);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/formatting");
    REQUIRE(request["params"]["options"]["insertSpaces"] == true);
    REQUIRE(request["params"]["options"].contains("tabSize"));

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 5}}}, {"end", {{"line", 0}, {"character", 6}}}}}, {"newText", " = "}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.has_value());
    REQUIRE(got->size() == 1);
    REQUIRE((*got)[0].newText == " = ");
}

TEST_CASE("LspManager::RequestFormatting resolves nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                                        invoked = false;
    std::optional<std::vector<ned::editor::lsp::WorkspaceTextEdit>> got;
    manager.RequestFormatting(buffer, [&](std::optional<std::vector<ned::editor::lsp::WorkspaceTextEdit>> edits) {
        invoked = true;
        got     = std::move(edits);
    });

    REQUIRE(invoked);
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("LspManager::RequestRangeFormatting sends textDocument/rangeFormatting with the requested range", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-range-formatting-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x=1;\nint y=2;");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                                        invoked = false;
    std::optional<std::vector<ned::editor::lsp::WorkspaceTextEdit>> got;
    manager.RequestRangeFormatting(buffer, 0, 8, [&](std::optional<std::vector<ned::editor::lsp::WorkspaceTextEdit>> edits) {
        invoked = true;
        got     = std::move(edits);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/rangeFormatting");
    REQUIRE(request["params"]["range"]["start"]["line"] == 0);
    REQUIRE(request["params"]["range"]["end"]["line"] == 0);

    const Json response = {{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"result", Json::array()}};
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.has_value());
    REQUIRE(got->empty());
}

TEST_CASE("LspManager::RequestSwitchSourceHeader sends a bare TextDocumentIdentifier and resolves a string uri response",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-switch-header-test.cpp";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                 invoked = false;
    std::optional<std::filesystem::path> got;
    manager.RequestSwitchSourceHeader(buffer, [&](std::optional<std::filesystem::path> path) {
        invoked = true;
        got     = path;
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/switchSourceHeader");
    REQUIRE(request["params"].contains("uri"));
    REQUIRE_FALSE(request["params"].contains("textDocument")); // bare TextDocumentIdentifier, not wrapped

    const std::filesystem::path headerPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-switch-header-test.h";
    const Json                  response   = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", "file://" + headerPath.string()},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.has_value());
    REQUIRE(*got == headerPath);
}

TEST_CASE("LspManager::RequestSwitchSourceHeader resolves to nullopt on a null result (no counterpart)", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-switch-header-null-test.cpp";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                 invoked = false;
    std::optional<std::filesystem::path> got;
    manager.RequestSwitchSourceHeader(buffer, [&](std::optional<std::filesystem::path> path) {
        invoked = true;
        got     = path;
    });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", nullptr},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("LspManager::RequestSwitchSourceHeader resolves to nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                 invoked = false;
    std::optional<std::filesystem::path> got;
    manager.RequestSwitchSourceHeader(buffer, [&](std::optional<std::filesystem::path> path) {
        invoked = true;
        got     = path;
    });

    REQUIRE(invoked);
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("LspManager::RequestRename sends newName and resolves a multi-file WorkspaceEdit's uris to real paths", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-rename-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("old_name");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    const std::string didOpen = ReadRawFrame(server.serverStdinRead);
    const std::string ownUri  = Json::parse(didOpen.substr(didOpen.find("\r\n\r\n") + 4))["params"]["textDocument"]["uri"].get<std::string>();

    bool                                      invoked = false;
    std::optional<LspManager::ResolvedRename> got;
    manager.RequestRename(buffer, 0, "new_name", [&](std::optional<LspManager::ResolvedRename> result) {
        invoked = true;
        got     = std::move(result);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/rename");
    REQUIRE(request["params"]["newName"] == "new_name");

    const std::filesystem::path otherPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-rename-other.txt";
    const Json                  response  = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result",
         {{"changes",
           {
               {ownUri, Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 8}}}}},
                                      {"newText", "new_name"}}})},
               {"file://" + otherPath.string(),
                Json::array({{{"range", {{"start", {{"line", 1}, {"character", 2}}}, {"end", {{"line", 1}, {"character", 10}}}}},
                              {"newText", "new_name"}}})},
           }}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.has_value());
    REQUIRE(got->hasEdit);
    REQUIRE_FALSE(got->touchesUnsupportedForm);
    REQUIRE(got->edits.size() == 2);
}

TEST_CASE("LspManager::RequestRename resolves to nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                      invoked = false;
    std::optional<LspManager::ResolvedRename> got;
    manager.RequestRename(buffer, 0, "new_name", [&](std::optional<LspManager::ResolvedRename> result) {
        invoked = true;
        got     = result;
    });

    REQUIRE(invoked);
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("BuildInitializeParams advertises codeActionLiteralSupport alongside the resolve capabilities", "[Lsp]") {
    // Regression test: without codeActionLiteralSupport a spec-following
    // server (clangd included) may only return bare Command objects -- no
    // "edit", no "kind" -- so every "fix available" quickfix listed fine but
    // applied as "has no edit to apply". See BuildInitializeParams' own
    // comment in LspManager.cpp.
    const Json params = ned::editor::lsp::BuildInitializeParams(std::filesystem::path("/some/project"));

    REQUIRE(params["rootUri"] == "file:///some/project");
    REQUIRE(params["processId"].is_number_integer());

    const Json& codeAction = params.at("capabilities").at("textDocument").at("codeAction");
    const Json& valueSet   = codeAction.at("codeActionLiteralSupport").at("codeActionKind").at("valueSet");
    REQUIRE(valueSet.is_array());
    REQUIRE(std::find(valueSet.begin(), valueSet.end(), Json("quickfix")) != valueSet.end());

    // The pre-existing resolve capabilities must survive the restructuring.
    REQUIRE(codeAction.at("dataSupport") == true);
    REQUIRE(codeAction.at("resolveSupport").at("properties") == Json::array({"edit"}));

    // workDoneProgress-support follow-up: invites $/progress reporting.
    REQUIRE(params.at("capabilities").at("window").at("workDoneProgress") == true);
}

// capabilities-hygiene follow-up: regression test for the gap the LSP
// coverage survey found -- this client sends/handles hover, definition,
// declaration, typeDefinition, implementation, references, rename,
// signatureHelp, publishDiagnostics, workspace/configuration, and
// workspace/executeCommand, but previously declared capabilities for none
// of them (only completion/codeAction/window.workDoneProgress existed).
TEST_CASE("BuildInitializeParams declares capabilities for every request/notification this client actually sends", "[Lsp]") {
    const Json params = ned::editor::lsp::BuildInitializeParams(std::filesystem::path("/some/project"));

    const Json& textDocument = params.at("capabilities").at("textDocument");
    for (const char* key :
         {"hover", "signatureHelp", "declaration", "definition", "typeDefinition", "implementation", "references", "rename",
          "publishDiagnostics", "callHierarchy", "typeHierarchy"}) {
        REQUIRE(textDocument.contains(key));
    }

    const Json& workspace = params.at("capabilities").at("workspace");
    REQUIRE(workspace.at("configuration") == true);
    REQUIRE(workspace.contains("didChangeConfiguration"));
    REQUIRE(workspace.contains("executeCommand"));
}

TEST_CASE("BuildInitializeParams absolutizes a relative rootUri", "[Lsp]") {
    // PathToUri (file-local in LspManager.cpp, reached through
    // BuildInitializeParams here) must never emit a relative file:// URI --
    // "file://demo.cpp" is unresolvable, and clangd rejects every request
    // naming one. A buffer opened via a relative CLI argument is the real
    // case; rootUri exercises the same helper.
    const Json params = ned::editor::lsp::BuildInitializeParams(std::filesystem::path("relative/dir"));

    const std::string rootUri = params["rootUri"].get<std::string>();
    REQUIRE(rootUri.rfind("file:///", 0) == 0);
    REQUIRE(rootUri.find("relative/dir") != std::string::npos);
}

TEST_CASE("BuildInitializeParams sends rootUri null for an empty project root", "[Lsp]") {
    // A real SIGABRT from a core dump: an empty ProjectRoot() reached
    // PathToUri, whose absolute("") throws, with no catch anywhere above
    // the Paint()-driven handshake -- the whole editor aborted on first
    // paint. The LSP spec explicitly allows "rootUri: DocumentUri | null",
    // so an empty root degrades to null rather than throwing or emitting a
    // nonsense "file://" URI.
    const Json params = ned::editor::lsp::BuildInitializeParams(std::filesystem::path());

    REQUIRE(params.contains("rootUri"));
    REQUIRE(params["rootUri"].is_null());
}

TEST_CASE("BuildInitializeParams omits initializationOptions when none is given", "[Lsp]") {
    const Json params = ned::editor::lsp::BuildInitializeParams(std::filesystem::path("/some/project"));
    REQUIRE_FALSE(params.contains("initializationOptions"));
}

TEST_CASE("BuildInitializeParams merges a non-empty initializationOptions verbatim", "[Lsp]") {
    // project-settings-lsp-init-options follow-up: e.g. a PHP project that
    // always preloads a bootstrap file before any real request runs, and
    // needs its language server told about that file via whatever shape its
    // own initializationOptions schema expects -- BuildInitializeParams
    // itself is unopinionated about the contents, just merges them in.
    const Json options = Json{{"bootstrapFiles", Json::array({"bootstrap.php"})}};
    const Json params  = ned::editor::lsp::BuildInitializeParams(std::filesystem::path("/some/project"), options);

    REQUIRE(params.contains("initializationOptions"));
    REQUIRE(params["initializationOptions"] == options);
}

TEST_CASE("LspManager tracks $/progress begin/report/end as LSP background activity with detail", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    REQUIRE(ned::editor::ActiveBackgroundActivities().empty());

    const Json begin = {{"jsonrpc", "2.0"},
                        {"method", "$/progress"},
                        {"params", {{"token", "backgroundIndexProgress"}, {"value", {{"kind", "begin"}, {"title", "indexing"}}}}}};
    client->DispatchFrame(begin.dump());
    auto active = ned::editor::ActiveBackgroundActivities();
    REQUIRE(active.size() == 1);
    REQUIRE(active[0].name == "LSP");
    REQUIRE(active[0].detail == "indexing");

    const Json report = {{"jsonrpc", "2.0"},
                         {"method", "$/progress"},
                         {"params", {{"token", "backgroundIndexProgress"}, {"value", {{"kind", "report"}, {"percentage", 45}}}}}};
    client->DispatchFrame(report.dump());
    active = ned::editor::ActiveBackgroundActivities();
    REQUIRE(active.size() == 1);
    REQUIRE(active[0].detail == "indexing (45%)");

    // A report for a token that never began must not resurrect anything later.
    const Json end = {{"jsonrpc", "2.0"},
                      {"method", "$/progress"},
                      {"params", {{"token", "backgroundIndexProgress"}, {"value", {{"kind", "end"}}}}}};
    client->DispatchFrame(end.dump());
    REQUIRE(ned::editor::ActiveBackgroundActivities().empty());

    client->DispatchFrame(end.dump()); // duplicate end -- must clamp, not go negative
    REQUIRE(ned::editor::ActiveBackgroundActivities().empty());
}

TEST_CASE("LspManager answers window/workDoneProgress/create with a null result", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    const Json request = {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "window/workDoneProgress/create"}, {"params", {{"token", "t"}}}};
    client->DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["id"] == 3);
    REQUIRE(response.contains("result"));
    REQUIRE(response["result"].is_null());
}

TEST_CASE("LspManager resolves workspace/configuration sections against lspWorkspaceConfiguration", "[Lsp]") {
    // project-settings-lsp-init-options follow-up: a config-pull server
    // (e.g. intelephense/phpactor-style) asks for its own section by dotted
    // path -- confirm both a top-level and a nested section resolve, and an
    // unconfigured one still falls back to null rather than erroring.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    const Json workspaceConfig = Json{{"phpactor", {{"file_extensions", Json::array({"php"})}}},
                                      {"intelephense", {{"environment", {{"includePaths", Json::array({"/stubs"})}}}}}};

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "php", eventLoop, client, workspaceConfig);

    const Json request = {{"jsonrpc", "2.0"},
                          {"id", 7},
                          {"method", "workspace/configuration"},
                          {"params",
                           {{"items", Json::array({{{"section", "phpactor"}}, {{"section", "intelephense.environment"}}, {{"section", "unconfigured.section"}}})}}}};
    client->DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["id"] == 7);
    REQUIRE(response["result"].is_array());
    REQUIRE(response["result"].size() == 3);
    CHECK(response["result"][0] == Json{{"file_extensions", Json::array({"php"})}});
    CHECK(response["result"][1] == Json{{"includePaths", Json::array({"/stubs"})}});
    CHECK(response["result"][2].is_null());
}

TEST_CASE("LspManager answers workspace/configuration with null for every item when nothing is configured", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    const Json request = {{"jsonrpc", "2.0"}, {"id", 9}, {"method", "workspace/configuration"}, {"params", {{"items", Json::array({{{"section", "anything"}}})}}}};
    client->DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["result"].is_array());
    REQUIRE(response["result"].size() == 1);
    CHECK(response["result"][0].is_null());
}

// prose-checking follow-up: the prose-checker connection is just another
// entry in the same clients_/bufferState_ maps under
// LspManager::kProseLanguageKey (see LspManager.h's own doc comment on that
// constant) -- SetClientForTesting works on it exactly like any other
// language, so these tests never touch ProseChecker.h's real
// auto-detect/enabled machinery at all (ProseCheckerTestGuard.cpp disables
// that globally for the whole ned_tests binary regardless).

TEST_CASE("SyncBuffer opens both the primary language server and the prose checker independently", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-sync-test.md";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("some text");

    LspClient* primaryClient = nullptr;
    LspClient* proseClient   = nullptr;
    FakeServer primaryServer = FakeServer::Create(manager, "markdown", eventLoop, primaryClient);
    FakeServer proseServer   = FakeServer::Create(manager, std::string(ned::editor::lsp::kProseLanguageKey), eventLoop, proseClient);

    manager.SyncBuffer(buffer, "markdown");

    const std::string primaryRaw  = ReadRawFrame(primaryServer.serverStdinRead);
    const Json        primaryOpen = Json::parse(primaryRaw.substr(primaryRaw.find("\r\n\r\n") + 4));
    REQUIRE(primaryOpen["method"] == "textDocument/didOpen");
    REQUIRE(primaryOpen["params"]["textDocument"]["languageId"] == "markdown");

    const std::string proseRaw  = ReadRawFrame(proseServer.serverStdinRead);
    const Json        proseOpen = Json::parse(proseRaw.substr(proseRaw.find("\r\n\r\n") + 4));
    REQUIRE(proseOpen["method"] == "textDocument/didOpen");
    // The prose checker's own didOpen carries the buffer's real language as
    // languageId, not the reserved "prose" server key -- harper-ls needs the
    // real language to know how to extract comments/strings from a document.
    REQUIRE(proseOpen["params"]["textDocument"]["languageId"] == "markdown");
}

TEST_CASE("Diagnostics published by the primary language server and the prose checker both land in Buffer::Diagnostics()",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-merge-test.md";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code teh");

    LspClient* primaryClient = nullptr;
    LspClient* proseClient   = nullptr;
    FakeServer primaryServer = FakeServer::Create(manager, "markdown", eventLoop, primaryClient);
    FakeServer proseServer   = FakeServer::Create(manager, std::string(ned::editor::lsp::kProseLanguageKey), eventLoop, proseClient);
    manager.SyncBuffer(buffer, "markdown");
    (void)ReadRawFrame(primaryServer.serverStdinRead); // drain didOpen
    (void)ReadRawFrame(proseServer.serverStdinRead);

    const std::string uri                = "file://" + path.string();
    const Json        primaryDiagnostics = {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params",
         {{"uri", uri},
          {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                        {"severity", 1},
                                        {"message", "syntax error"}}})}}},
    };
    primaryClient->DispatchFrame(primaryDiagnostics.dump());

    const Json proseDiagnostics = {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params",
         {{"uri", uri},
          {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 9}}}, {"end", {{"line", 0}, {"character", 12}}}}},
                                        {"severity", 4},
                                        {"message", "possible typo: teh"}}})}}},
    };
    proseClient->DispatchFrame(proseDiagnostics.dump());
    WaitForDiagnosticCount(eventLoop, buffer, 2);

    REQUIRE(buffer.Diagnostics().size() == 2); // neither server's publish clobbered the other's
    bool sawSyntaxError = false;
    bool sawTypo        = false;
    for (const Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        if (diagnostic.message == "syntax error") {
            sawSyntaxError = true;
            // prose-diagnostic-callout follow-up: the real ("markdown") server's
            // own diagnostic must stay tagged Code -- BufferView renders that
            // origin with the ordinary underline/inline-annotation treatment.
            REQUIRE(diagnostic.origin == Buffer::Diagnostic::Origin::Code);
        }
        else if (diagnostic.message == "possible typo: teh") {
            sawTypo = true;
            // The prose checker's own connection is keyed by kProseLanguageKey
            // regardless of the buffer's real language -- see
            // HandlePublishDiagnostics' own origin derivation.
            REQUIRE(diagnostic.origin == Buffer::Diagnostic::Origin::Prose);
        }
    }
    REQUIRE(sawSyntaxError);
    REQUIRE(sawTypo);
}

TEST_CASE("A second publish from one source replaces only that source's own diagnostics slice", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-reslice-test.md";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code teh");

    LspClient* primaryClient = nullptr;
    LspClient* proseClient   = nullptr;
    FakeServer primaryServer = FakeServer::Create(manager, "markdown", eventLoop, primaryClient);
    FakeServer proseServer   = FakeServer::Create(manager, std::string(ned::editor::lsp::kProseLanguageKey), eventLoop, proseClient);
    manager.SyncBuffer(buffer, "markdown");
    (void)ReadRawFrame(primaryServer.serverStdinRead);
    (void)ReadRawFrame(proseServer.serverStdinRead);

    const std::string uri                     = "file://" + path.string();
    auto              diagnosticsNotification = [&](const std::string& message) {
        return Json{
            {"jsonrpc", "2.0"},
            {"method", "textDocument/publishDiagnostics"},
            {"params",
             {{"uri", uri},
              {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                            {"severity", 1},
                                            {"message", message}}})}}},
        };
    };

    primaryClient->DispatchFrame(diagnosticsNotification("first error").dump());
    proseClient->DispatchFrame(diagnosticsNotification("possible typo: teh").dump());
    WaitForDiagnosticCount(eventLoop, buffer, 2);
    REQUIRE(buffer.Diagnostics().size() == 2);

    // Primary republishes its own full current set (a real server does this
    // on every didChange) -- only its own slice is replaced, the prose
    // checker's diagnostic from before must survive untouched.
    primaryClient->DispatchFrame(diagnosticsNotification("second error").dump());
    // Total count stays 2 across this replacement (one message swapped for
    // another), so WaitForDiagnosticCount's own count check can't detect
    // when the debounced application has actually landed -- wait on the
    // new message's content instead.
    WaitUntil(eventLoop, [&] {
        return std::any_of(buffer.Diagnostics().begin(), buffer.Diagnostics().end(),
                           [](const Buffer::Diagnostic& d) { return d.message == "second error"; });
    });

    REQUIRE(buffer.Diagnostics().size() == 2);
    bool sawSecondError = false;
    bool sawFirstError  = false;
    bool sawTypo        = false;
    for (const Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        sawSecondError |= diagnostic.message == "second error";
        sawFirstError |= diagnostic.message == "first error";
        sawTypo |= diagnostic.message == "possible typo: teh";
    }
    REQUIRE(sawSecondError);
    REQUIRE_FALSE(sawFirstError); // primary's own stale diagnostic is gone
    REQUIRE(sawTypo);             // prose's diagnostic from before is untouched
}

namespace {
// pull-diagnostics follow-up: same RAII shape as this codebase's other
// opt-in-toggle test guards (e.g. BufferViewTest.cpp's
// LspFormatOnSaveGuard).
struct PullDiagnosticsEnabledGuard {
    PullDiagnosticsEnabledGuard() {
        ned::editor::lsp::SetLspPullDiagnosticsEnabled(true);
    }
    ~PullDiagnosticsEnabledGuard() {
        ned::editor::lsp::SetLspPullDiagnosticsEnabled(false);
    }
};
} // namespace

TEST_CASE("A didOpen sync sends textDocument/diagnostic when lsp-pull-diagnostics is enabled, and a full report "
          "lands in Buffer::Diagnostics()",
          "[Lsp]") {
    const PullDiagnosticsEnabledGuard guard;
    BufferList                        bufferList;
    ned::ui::EventLoop                eventLoop;
    LspManager                        manager(bufferList, eventLoop);
    const std::filesystem::path       path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-pull-diagnostics-test.txt";
    Buffer&                           buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");

    // didOpen and the pull-diagnostics request are sent back-to-back,
    // synchronously -- both can land in the same read() (ReadRawFrame's own
    // one-frame-per-call assumption breaks here, same as ParseAllFrames'
    // own header comment describes for LspManager::Shutdown's frame pair).
    const std::string raw    = ReadRawFramesUntil(server.serverStdinRead, 2);
    const auto        frames = ParseAllFrames(raw);
    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0]["method"] == "textDocument/didOpen");
    REQUIRE(frames[1]["method"] == "textDocument/diagnostic");
    REQUIRE(frames[1]["params"]["textDocument"]["uri"] == "file://" + path.string());

    const auto response = Json{
        {"jsonrpc", "2.0"},
        {"id", frames[1]["id"]},
        {"result", {{"kind", "full"},
                    {"items", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                            {"severity", 1},
                                            {"message", "pulled error"}}})}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(buffer.Diagnostics().size() == 1);
    REQUIRE(buffer.Diagnostics()[0].message == "pulled error");
}

TEST_CASE("A server erroring on textDocument/diagnostic is never asked again for that connection's lifetime",
          "[Lsp]") {
    const PullDiagnosticsEnabledGuard guard;
    BufferList                        bufferList;
    ned::ui::EventLoop                eventLoop;
    LspManager                        manager(bufferList, eventLoop);
    const std::filesystem::path       path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-pull-diagnostics-unsupported-test.txt";
    Buffer&                           buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("a");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");

    // didOpen and the pull-diagnostics request land back-to-back in the
    // same read() -- see the sibling success-case test's own comment.
    const std::string raw    = ReadRawFramesUntil(server.serverStdinRead, 2);
    const auto        frames = ParseAllFrames(raw);
    REQUIRE(frames.size() == 2);
    REQUIRE(frames[1]["method"] == "textDocument/diagnostic");
    client->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", frames[1]["id"]}, {"error", {{"code", -32601}, {"message", "method not found"}}}}.dump());

    // A second content change re-syncs (didChange) but must not send a
    // second textDocument/diagnostic -- only NoFrameArrives can confirm
    // this safely (see its own doc comment: nothing else is queued to read).
    // sync-debounce follow-up: SyncBuffer no longer sends didChange
    // synchronously -- it (re)arms a per-(buffer, serverKey) DeadlineTimer
    // (LspSyncDebounceMs), same WaitUntil-polling idiom this file already
    // uses for the diagnostics debounce above.
    buffer.InsertAtPoint("b");
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string didChange = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(didChange.substr(didChange.find("\r\n\r\n") + 4))["method"] == "textDocument/didChange");
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("No textDocument/diagnostic request is sent when lsp-pull-diagnostics is disabled (the default)", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-pull-diagnostics-disabled-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen -- nothing else was ever queued behind it
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("RequestSemanticTokensFull sends a request when a legend is set and applies decoded, byte-resolved spans",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-tokens-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting(
        "test-lang", SemanticTokensLegend{.tokenTypes = {"keyword", "variable", "unknown"}, .tokenModifiers = {}});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    REQUIRE(manager.SemanticTokensGeneration(buffer) == 0);
    manager.RequestSemanticTokensFull(buffer, "test-lang");
    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const auto        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/semanticTokens/full");

    // "int" (keyword, type index 0) at [0,3), "x" (variable, type index 1) at [4,5) -- deltaStartChar relative
    // since deltaLine is 0. "unknown" (type index 2 -- present in the legend but unmapped, see
    // SyntaxClassForSemanticTokenType) at [6,7) must be dropped, not force-fit onto a class.
    const auto response = Json{
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", {{"data", Json::array({0, 0, 3, 0, 0, 0, 4, 1, 1, 0, 0, 2, 1, 2, 0})}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(manager.SemanticTokensGeneration(buffer) == 1);
    const std::vector<HighlightSpan>& spans = manager.SemanticTokenSpans(buffer);
    REQUIRE(spans.size() == 2);
    REQUIRE(spans[0].startByte == 0);
    REQUIRE(spans[0].endByte == 3);
    REQUIRE(spans[0].syntaxClass == SyntaxClass::Keyword);
    REQUIRE(spans[1].startByte == 4);
    REQUIRE(spans[1].endByte == 5);
    REQUIRE(spans[1].syntaxClass == SyntaxClass::Variable);
}

TEST_CASE("RequestSemanticTokensFull sends nothing when the server never advertised a legend", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-tokens-no-legend-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen -- nothing else was ever queued behind it

    manager.RequestSemanticTokensFull(buffer, "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));
    REQUIRE(manager.SemanticTokenSpans(buffer).empty());
}

TEST_CASE("RequestSemanticTokensFull does not resend for unchanged content", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-tokens-dedup-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting("test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestSemanticTokensFull(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // the one real request

    // Called again with no intervening edit -- BufferView calls this once
    // per Paint(), and a cursor-blink/scroll-only repaint must not resend.
    manager.RequestSemanticTokensFull(buffer, "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("RequestSemanticTokensFull sends nothing when semantic highlighting is disabled", "[Lsp]") {
    ned::editor::lsp::SetLspSemanticHighlightingEnabled(false);
    struct RestoreGuard {
        ~RestoreGuard() {
            ned::editor::lsp::SetLspSemanticHighlightingEnabled(true);
        }
    } restore;

    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-tokens-disabled-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting("test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestSemanticTokensFull(buffer, "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("RequestInlayHints sends the viewport range and applies byte-resolved, sorted hints", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestInlayHints(buffer, 0, buffer.Size(), "test-lang");
    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const auto        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/inlayHint");
    REQUIRE(request["params"]["range"]["start"]["character"] == 0);

    // Two hints, sent out of order -- confirms InlayHintSpans sorts by
    // byteOffset rather than trusting response order.
    const auto response = Json{
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"position", {{"line", 0}, {"character", 9}}}, {"label", ": int"}},
                                {{"position", {{"line", 0}, {"character", 3}}}, {"label", ": int"}}})},
    };
    client->DispatchFrame(response.dump());

    const std::vector<LspManager::ResolvedInlayHint>& hints = manager.InlayHintSpans(buffer);
    REQUIRE(hints.size() == 2);
    REQUIRE(hints[0].byteOffset == 3);
    REQUIRE(hints[1].byteOffset == 9);
}

TEST_CASE("RequestInlayHints does not resend for the same (content, viewport), but does resend for a different "
          "viewport",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-dedup-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;\nint y = 2;\n");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestInlayHints(buffer, 0, 11, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // the one real request for this range

    manager.RequestInlayHints(buffer, 0, 11, "test-lang"); // same range, no edit -- a repaint, not a real change
    REQUIRE(NoFrameArrives(server.serverStdinRead));

    manager.RequestInlayHints(buffer, 11, 22, "test-lang"); // scrolled to reveal new content
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["method"] == "textDocument/inlayHint");
}

TEST_CASE("A server erroring on textDocument/inlayHint is never asked again for that connection's lifetime", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-unsupported-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("a");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestInlayHints(buffer, 0, 1, "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"error", {{"code", -32601}, {"message", "method not found"}}}}.dump());

    // sync-debounce follow-up: SyncBuffer no longer sends didChange
    // synchronously -- see the sibling diagnostics-unsupported test's own
    // comment for why WaitUntil is needed here now.
    buffer.InsertAtPoint("b");
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string didChange = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(didChange.substr(didChange.find("\r\n\r\n") + 4))["method"] == "textDocument/didChange");
    manager.RequestInlayHints(buffer, 0, 2, "test-lang"); // different viewport too -- would resend if not latched
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("No textDocument/inlayHint request is sent when lsp-inlay-hints is disabled", "[Lsp]") {
    ned::editor::lsp::SetLspInlayHintsEnabled(false);
    struct RestoreGuard {
        ~RestoreGuard() {
            ned::editor::lsp::SetLspInlayHintsEnabled(true);
        }
    } restore;

    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-disabled-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestInlayHints(buffer, 0, buffer.Size(), "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("NotifyBufferClosed sends didClose to every server the buffer was opened with", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-close-test.md";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("some text");

    LspClient* primaryClient = nullptr;
    LspClient* proseClient   = nullptr;
    FakeServer primaryServer = FakeServer::Create(manager, "markdown", eventLoop, primaryClient);
    FakeServer proseServer   = FakeServer::Create(manager, std::string(ned::editor::lsp::kProseLanguageKey), eventLoop, proseClient);
    manager.SyncBuffer(buffer, "markdown");
    (void)ReadRawFrame(primaryServer.serverStdinRead); // drain didOpen
    (void)ReadRawFrame(proseServer.serverStdinRead);

    manager.NotifyBufferClosed(buffer);

    const std::string primaryRaw   = ReadRawFrame(primaryServer.serverStdinRead);
    const Json        primaryClose = Json::parse(primaryRaw.substr(primaryRaw.find("\r\n\r\n") + 4));
    REQUIRE(primaryClose["method"] == "textDocument/didClose");

    const std::string proseRaw   = ReadRawFrame(proseServer.serverStdinRead);
    const Json        proseClose = Json::parse(proseRaw.substr(proseRaw.find("\r\n\r\n") + 4));
    REQUIRE(proseClose["method"] == "textDocument/didClose");
}

TEST_CASE("SyncBuffer never opens the prose checker for a binary buffer, but the primary language server still opens normally",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-binary-test.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out.put('\0');
        out << "some content after a nul byte";
    }
    Buffer& buffer = bufferList.OpenOrCreateFile(path, /*allowBinary=*/true);

    LspClient* primaryClient = nullptr;
    LspClient* proseClient   = nullptr;
    FakeServer primaryServer = FakeServer::Create(manager, "fundamental", eventLoop, primaryClient);
    FakeServer proseServer   = FakeServer::Create(manager, std::string(ned::editor::lsp::kProseLanguageKey), eventLoop, proseClient);

    manager.SyncBuffer(buffer, "fundamental");

    // The binary skip is scoped to the prose checker only -- the primary
    // language server still opens the buffer exactly as it always has.
    const std::string primaryRaw = ReadRawFrame(primaryServer.serverStdinRead);
    REQUIRE(primaryRaw.find("textDocument/didOpen") != std::string::npos);

    REQUIRE(NoFrameArrives(proseServer.serverStdinRead));
}

// embedded-language-documents follow-up: below this point, tests for
// SyncEmbeddedDocuments, the PrimarySyncState fix it required, diagnostics
// filtering by owned range, and serverKey routing on the four requests that
// previously only ever resolved to PrimarySyncState.

TEST_CASE("SyncEmbeddedDocuments opens an embedded server independently of the primary, sending the given text verbatim",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-embedded-open-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    LspClient* htmlClient = nullptr;
    LspClient* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html");
    (void)ReadRawFrame(htmlServer.serverStdinRead); // drain html's own didOpen

    REQUIRE(NoFrameArrives(jsServer.serverStdinRead)); // not yet embedded-synced

    const std::string paddedText = "        let x = 1;          "; // stands in for real padding -- content unimportant here
    manager.SyncEmbeddedDocuments(
        buffer, {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = paddedText, .ownedRanges = {{8, 19}}}});

    const std::string raw    = ReadRawFrame(jsServer.serverStdinRead);
    const Json        opened = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(opened["method"] == "textDocument/didOpen");
    REQUIRE(opened["params"]["textDocument"]["text"] == paddedText);
    REQUIRE(opened["params"]["textDocument"]["languageId"] == "javascript");

    const auto activeKeys = manager.ActiveServerKeysForBuffer(buffer);
    REQUIRE(std::find(activeKeys.begin(), activeKeys.end(), "html") != activeKeys.end());
    REQUIRE(std::find(activeKeys.begin(), activeKeys.end(), "javascript") != activeKeys.end());
}

TEST_CASE("SyncEmbeddedDocuments sends an incremental didChange against the embedded document's own previous text",
          "[Lsp]") {
    // incremental-sync follow-up: the embedded key's own bufferState_ entry
    // must diff against *its own* lastSyncedText (the previously padded
    // virtual document), never the host buffer's.
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ned-lsp-manager-embedded-incremental-test.html";
    Buffer& buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    LspClient* htmlClient = nullptr;
    LspClient* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);
    manager.SetTextDocumentSyncKindForTesting("javascript", TextDocumentSyncKind::Incremental);

    manager.SyncBuffer(buffer, "html");
    (void)ReadRawFrame(htmlServer.serverStdinRead); // drain html's own didOpen

    const std::string firstPadded = "        let x = 1;          ";
    manager.SyncEmbeddedDocuments(
        buffer, {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = firstPadded, .ownedRanges = {{8, 19}}}});
    (void)ReadRawFrame(jsServer.serverStdinRead); // drain javascript's own didOpen

    // SyncTextToServer's own generation gate is keyed on the *host buffer's*
    // ContentGeneration(), not documentText -- a real caller always re-edits
    // the buffer before resolving new embedded regions and calling this
    // again (BufferView::Paint()), so this edit (content irrelevant, only
    // its generation bump matters) mirrors that, otherwise the second call
    // below is silently skipped as "nothing changed since the last sync".
    buffer.InsertAtPoint(" ");

    // A different padding of a slightly longer real edit -- differs from
    // firstPadded both inside and outside its "real" content span.
    const std::string secondPadded = "        let x = 12;           ";
    manager.SyncEmbeddedDocuments(
        buffer,
        {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = secondPadded, .ownedRanges = {{8, 20}}}});

    const std::string raw   = ReadRawFrame(jsServer.serverStdinRead);
    const Json        frame = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(frame["method"] == "textDocument/didChange");
    const Json& change = frame["params"]["contentChanges"][0];
    REQUIRE(change.contains("range")); // diffed against firstPadded, not the host buffer's own text
    // Applying the reported change to firstPadded (the embedded document's
    // own previous text) must reproduce secondPadded exactly.
    const std::size_t startChar = change["range"]["start"]["character"].get<std::size_t>();
    const std::size_t endChar   = change["range"]["end"]["character"].get<std::size_t>();
    const std::string reconstructed =
        firstPadded.substr(0, startChar) + change["text"].get<std::string>() + firstPadded.substr(std::min(endChar, firstPadded.size()));
    REQUIRE(reconstructed == secondPadded);
}

TEST_CASE("SyncEmbeddedDocuments tears down a server key whose region disappeared: didClose sent, its diagnostics dropped",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-embedded-teardown-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    LspClient* htmlClient = nullptr;
    LspClient* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html");
    (void)ReadRawFrame(htmlServer.serverStdinRead);
    manager.SyncEmbeddedDocuments(
        buffer, {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = "let x = 1;", .ownedRanges = {{0, 10}}}});
    (void)ReadRawFrame(jsServer.serverStdinRead); // drain didOpen

    // A javascript diagnostic lands while the region still exists.
    const Json diagnosticsParams = {
        {"uri", "file://" + path.string()},
        {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                                      {"severity", 1},
                                      {"message", "unused variable"}}})},
    };
    jsClient->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"}, {"params", diagnosticsParams}}.dump());
    WaitForDiagnosticCount(eventLoop, buffer, 1);

    // The <script> block is gone -- the next sync reports no javascript document at all.
    manager.SyncEmbeddedDocuments(buffer, {});

    const std::string closeRaw = ReadRawFrame(jsServer.serverStdinRead);
    const Json        closed   = Json::parse(closeRaw.substr(closeRaw.find("\r\n\r\n") + 4));
    REQUIRE(closed["method"] == "textDocument/didClose");

    WaitForDiagnosticCount(eventLoop, buffer, 0); // the stale javascript diagnostic must not linger

    const auto activeKeys = manager.ActiveServerKeysForBuffer(buffer);
    REQUIRE(std::find(activeKeys.begin(), activeKeys.end(), "javascript") == activeKeys.end());
    REQUIRE(std::find(activeKeys.begin(), activeKeys.end(), "html") != activeKeys.end()); // primary untouched
}

TEST_CASE(
    "PrimarySyncState fix regression: with host, prose, and an embedded key all synced, a default-serverKey request still "
    "resolves to the host server",
    "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-primary-ambiguity-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    LspClient* htmlClient  = nullptr;
    LspClient* proseClient = nullptr;
    LspClient* jsClient    = nullptr;
    FakeServer htmlServer  = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer proseServer = FakeServer::Create(manager, std::string(kProseLanguageKey), eventLoop, proseClient);
    FakeServer jsServer    = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html"); // primary ("html") + prose
    manager.SyncEmbeddedDocuments(
        buffer, {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = "let x = 1;", .ownedRanges = {{0, 10}}}});
    (void)ReadRawFrame(htmlServer.serverStdinRead);
    (void)ReadRawFrame(proseServer.serverStdinRead);
    (void)ReadRawFrame(jsServer.serverStdinRead);

    // Default (empty) serverKey must resolve to "html" -- with three
    // simultaneous bufferState_ entries (html, prose, javascript), the old
    // "whichever entry isn't kProseLanguageKey" scan could just as easily
    // have picked "javascript" first, silently sending a hover request at
    // the wrong server.
    manager.RequestHover(buffer, 0, [](std::optional<std::string>) {});
    const std::string raw = ReadRawFrame(htmlServer.serverStdinRead);
    REQUIRE(raw.find("textDocument/hover") != std::string::npos);
    REQUIRE(NoFrameArrives(proseServer.serverStdinRead));
    REQUIRE(NoFrameArrives(jsServer.serverStdinRead));
}

TEST_CASE("HandlePublishDiagnostics drops an embedded server's diagnostic whose start falls outside every owned range",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-embedded-diag-filter-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<div></div><script>let x = 1;</script>");

    LspClient* jsClient = nullptr;
    FakeServer jsServer = FakeServer::Create(manager, "javascript", eventLoop, jsClient);
    // Owned range covers only the "let x = 1;" content (offsets 20..30 in
    // the buffer above); everything else is padding as far as javascript is
    // concerned.
    manager.SyncEmbeddedDocuments(
        buffer, {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = buffer.Text(), .ownedRanges = {{20, 30}}}});
    (void)ReadRawFrame(jsServer.serverStdinRead);

    const Json diagnosticsParams = {
        {"uri", "file://" + path.string()},
        {"diagnostics",
         Json::array({
             // Inside the owned range -- kept.
             {{"range", {{"start", {{"line", 0}, {"character", 20}}}, {"end", {{"line", 0}, {"character", 23}}}}},
              {"severity", 1},
              {"message", "kept: inside owned range"}},
             // Outside the owned range (in the padded <div></div> prefix) -- dropped.
             {{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 5}}}}},
              {"severity", 1},
              {"message", "dropped: outside owned range"}},
         })},
    };
    jsClient->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"method", "textDocument/publishDiagnostics"}, {"params", diagnosticsParams}}.dump());
    WaitForDiagnosticCount(eventLoop, buffer, 1);

    REQUIRE(buffer.Diagnostics()[0].message == "kept: inside owned range");
}

TEST_CASE("LspManager::RequestHover with an explicit serverKey routes to that connection, not the primary one", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-hover-serverkey-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    LspClient* htmlClient = nullptr;
    LspClient* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html");
    manager.SyncEmbeddedDocuments(
        buffer, {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = "let x = 1;", .ownedRanges = {{0, 10}}}});
    (void)ReadRawFrame(htmlServer.serverStdinRead);
    (void)ReadRawFrame(jsServer.serverStdinRead);

    bool                       invoked = false;
    std::optional<std::string> gotText;
    manager.RequestHover(
        buffer, 0, [&](std::optional<std::string> text) {
            invoked = true;
            gotText = std::move(text);
        },
        "javascript");

    REQUIRE(NoFrameArrives(htmlServer.serverStdinRead)); // never asked html

    const std::string raw = ReadRawFrame(jsServer.serverStdinRead);
    REQUIRE(raw.find("textDocument/hover") != std::string::npos);
    const Json request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", request["id"]},
        {"result", {{"contents", "let x: number"}}},
    };
    jsClient->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(gotText == std::optional<std::string>("let x: number"));
}

TEST_CASE("LspManager::RequestDefinition with an explicit serverKey routes to that connection, not the primary one", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    LspManager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-definition-serverkey-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>call_site();</script>");

    LspClient* htmlClient = nullptr;
    LspClient* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html");
    manager.SyncEmbeddedDocuments(
        buffer, {LspManager::EmbeddedDocumentSync{.language = "javascript", .documentText = "call_site();", .ownedRanges = {{0, 12}}}});
    (void)ReadRawFrame(htmlServer.serverStdinRead);
    (void)ReadRawFrame(jsServer.serverStdinRead);

    bool                                      invoked = false;
    std::vector<LspManager::ResolvedLocation> got;
    manager.RequestDefinition(
        buffer, 0,
        [&](std::vector<LspManager::ResolvedLocation> locations) {
            invoked = true;
            got     = std::move(locations);
        },
        "javascript");

    REQUIRE(NoFrameArrives(htmlServer.serverStdinRead));

    const std::string raw = ReadRawFrame(jsServer.serverStdinRead);
    REQUIRE(raw.find("textDocument/definition") != std::string::npos);
    const Json request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));

    const std::filesystem::path definitionPath = std::filesystem::temp_directory_path() / "ned-lsp-manager-definition-serverkey-target.js";
    const Json                  response       = {
        {"jsonrpc", "2.0"},
        {"id", request["id"]},
        {"result", Json::array({{{"uri", "file://" + definitionPath.string()},
                                 {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 4}}}}}}})},
    };
    jsClient->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].path == definitionPath);
}

// LSP-deliberate-cuts follow-up: LspBackgroundSyncEnabled is process-wide
// state (see LspBackgroundSync.h) -- every test that flips it must leave it
// default-on for the next test, AutoRevertTest.cpp's own RAII-guard pattern.
namespace {
struct LspBackgroundSyncGuard {
    ~LspBackgroundSyncGuard() {
        ned::editor::lsp::SetLspBackgroundSyncEnabled(true);
    }
};
} // namespace

TEST_CASE("SyncBackgroundBuffers syncs every open, path-backed buffer, not just one", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    const std::filesystem::path cPath  = std::filesystem::temp_directory_path() / "ned-lsp-background-sync-test.c";
    const std::filesystem::path pyPath = std::filesystem::temp_directory_path() / "ned-lsp-background-sync-test.py";
    Buffer&                     cBuffer  = bufferList.OpenOrCreateFile(cPath);
    Buffer&                     pyBuffer = bufferList.OpenOrCreateFile(pyPath);

    LspClient* cClient  = nullptr;
    LspClient* pyClient = nullptr;
    FakeServer cServer  = FakeServer::Create(manager, "c", eventLoop, cClient);
    FakeServer pyServer = FakeServer::Create(manager, "python", eventLoop, pyClient);

    ned::editor::lsp::SyncBackgroundBuffers(bufferList, manager);

    const std::string cRaw  = ReadRawFrame(cServer.serverStdinRead);
    const Json        cOpen = Json::parse(cRaw.substr(cRaw.find("\r\n\r\n") + 4));
    REQUIRE(cOpen["method"] == "textDocument/didOpen");
    REQUIRE(cOpen["params"]["textDocument"]["languageId"] == "c");

    const std::string pyRaw  = ReadRawFrame(pyServer.serverStdinRead);
    const Json        pyOpen = Json::parse(pyRaw.substr(pyRaw.find("\r\n\r\n") + 4));
    REQUIRE(pyOpen["method"] == "textDocument/didOpen");
    REQUIRE(pyOpen["params"]["textDocument"]["languageId"] == "python");
}

TEST_CASE("SyncBackgroundBuffers skips a buffer with no path and a buffer still loading", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    Buffer& scratch = bufferList.CreateBuffer("scratch"); // no path
    (void)scratch;

    const std::filesystem::path loadingPath = std::filesystem::temp_directory_path() / "ned-lsp-background-sync-loading-test.c";
    Buffer&                     loading     = bufferList.OpenOrCreateFile(loadingPath);
    loading.MarkLoading();

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "c", eventLoop, client);

    ned::editor::lsp::SyncBackgroundBuffers(bufferList, manager); // must not crash and must not sync either buffer

    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("SyncBackgroundBuffers is a no-op entirely when disabled", "[Lsp]") {
    LspBackgroundSyncGuard guard;
    BufferList              bufferList;
    ned::ui::EventLoop      eventLoop;
    LspManager              manager(bufferList, eventLoop);

    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-background-sync-disabled-test.c";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "c", eventLoop, client);

    ned::editor::lsp::SetLspBackgroundSyncEnabled(false);
    ned::editor::lsp::SyncBackgroundBuffers(bufferList, manager);

    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

// graceful-lsp-shutdown follow-up.
TEST_CASE("LspManager::Shutdown sends shutdown then exit to a directly-spawned client", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client); // brokerBacked defaults to false

    manager.Shutdown();

    const std::vector<Json> frames = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 2));
    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0]["method"] == "shutdown");
    REQUIRE(frames[1]["method"] == "exit");
}

TEST_CASE("LspManager::Shutdown never sends anything to a broker-backed client", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    LspManager         manager(bufferList, eventLoop);

    LspClient* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client, Json::object(), /*brokerBacked=*/true);

    manager.Shutdown();

    // A broker-owned server is shared with other ned processes and the
    // broker daemon itself -- it must keep running after this process
    // exits, so it must never receive this process's own shutdown/exit.
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}
