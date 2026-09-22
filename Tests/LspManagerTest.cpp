#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <poll.h>
#include <unistd.h>

#include "Editor/BackgroundActivity.h"
#include "Editor/Lsp/BackgroundSync.h"
#include "Editor/Lsp/Client.h"
#include "Editor/Lsp/Manager.h"
#include "Editor/Lsp/RootResolver.h"
#include "Editor/Lsp/ServerConfig.h"
#include "Editor/Lsp/Transport.h"
#include "Editor/Project/Root.h"
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
using ned::editor::lsp::Client;
using ned::editor::lsp::Manager;
using ned::editor::lsp::SemanticTokensLegend;
using ned::editor::lsp::SetLspRootMarkers;
using ned::editor::lsp::SetLspWorkspaceFoldersEnabled;
using ned::editor::lsp::TextDocumentSyncKind;
using ned::editor::lsp::Transport;
using ned::text::Buffer;
using ned::text::BufferList;

namespace {

// Mirrors ClientTest.cpp's own ClientFixture exactly (see that file's
// header comment for the full rationale, including why serverStdoutWrite
// must be closed before the Client it feeds) -- a raw pipe pair standing
// in for a real language server's stdin/stdout, used here to drive
// Manager::SetClientForTesting instead of Client directly.
struct FakeServer {
    int serverStdinRead;   // test reads what the client wrote
    int serverStdoutWrite; // test writes to feed the client's (unused, in these tests) read thread

    FakeServer(int readFd, int writeFd) : serverStdinRead(readFd), serverStdoutWrite(writeFd) {
    }

    ~FakeServer() {
        if (serverStdoutWrite >= 0) {
            ::close(serverStdoutWrite);
        }
        if (serverStdinRead >= 0) {
            ::close(serverStdinRead);
        }
    }

    FakeServer(const FakeServer&)            = delete;
    FakeServer& operator=(const FakeServer&) = delete;

    // Deliberately not `= default`: a defaulted move copies the raw fd ints
    // and leaves the source owning them too, so the moved-from temporary's
    // destructor closes fds the destination still believes it holds. Benign
    // for a single FakeServer (nothing reclaims the numbers), silently
    // catastrophic the moment a second one is created afterwards -- its
    // pipe() call reuses exactly those freed fd numbers, and the first
    // server's reads then block on, or steal from, the second's pipe.
    FakeServer(FakeServer&& other) noexcept
        : serverStdinRead(std::exchange(other.serverStdinRead, -1)),
          serverStdoutWrite(std::exchange(other.serverStdoutWrite, -1)) {
    }

    static FakeServer Create(Manager& manager, const std::string& language, ned::ui::EventLoop& eventLoop, Client*& outClient,
                             const Json& workspaceConfiguration = Json::object(), bool brokerBacked = false,
                             std::optional<std::string> connectionKeyOverride = std::nullopt) {
        int clientWritesHere[2]; // client's write end -> test's read end
        int clientReadsHere[2];  // test's write end -> client's read end
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        auto client = std::make_unique<Client>(Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop);
        outClient   = &manager.SetClientForTesting(language, std::move(client), workspaceConfiguration, brokerBacked,
                                                   std::move(connectionKeyOverride));
        return FakeServer(clientWritesHere[0], clientReadsHere[1]);
    }
};

// Reads exactly one LSP frame's raw bytes off a plain fd -- copied from
// ClientTest.cpp's own ReadRawFrame (kept file-local here too rather than
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
// land in the same read() (Manager::Shutdown's own shutdown+exit pair,
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
// ServerConfig.h's DiagnosticsDebounceMs) whose fire is Post()ed onto
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

TEST_CASE("Manager::RequestHover resolves synchronously to nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
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

TEST_CASE("Manager::RequestCompletion resolves synchronously to an empty list when the buffer was never synced",
          "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                        invoked = false;
    std::vector<CompletionItem> gotItems;
    manager.RequestCompletion(buffer, 0, [&](ned::editor::lsp::CompletionList list) {
        invoked  = true;
        gotItems = std::move(list.items);
    });

    REQUIRE(invoked);
    REQUIRE(gotItems.empty());
}

TEST_CASE("Manager::SyncBuffer is a no-op for a buffer with no associated path", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch"); // no path -- Buffer::Path() == nullopt

    ned::editor::lsp::SetLspServerCommand("test-lang", {"/bin/cat"});
    manager.SyncBuffer(buffer, "test-lang"); // must not spawn anything or crash

    bool invoked = false;
    manager.RequestHover(buffer, 0, [&](std::optional<std::string>) { invoked = true; });
    REQUIRE(invoked); // still resolves synchronously -- SyncBuffer never actually opened it

    ned::editor::lsp::SetLspServerCommand("test-lang", {}); // clean up global config state for other tests
}

TEST_CASE("Manager::SyncBuffer is a no-op when nothing is configured for the buffer's language", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
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
// Manager::SyncToServer used to call buffer.Text() (a full
// Storage_->ToString() materialization) unconditionally, before ever
// checking whether a client is configured for the target language. For a
// huge buffer with no server configured, SyncBackgroundBuffers' periodic
// tick (Source/Editor/Lsp/BackgroundSync.cpp) paid that full-document
// copy on every single tick for nothing -- at multi-GB scale this made
// each tick take longer than the tick interval itself, backing up
// EventLoop::Post forever and hanging the whole editor. Fixed by moving
// the ClientForLanguage check ahead of the buffer.Text() argument in
// SyncToServer. This test proves the fix holds: syncing a huge buffer
// against an unconfigured language must not materialize its content.
TEST_CASE("Manager::SyncBuffer does not materialize a huge buffer's content when no server is configured",
          "[Lsp][memory]") {
    constexpr std::size_t kFileSize = 200 * 1024 * 1024; // 200 MiB -- same "obviously wrong if resident" size the
                                                         // sibling PieceTable/BufferHugeFile [memory] tests use

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_lsp_manager_huge_nosync.txt";
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
    Manager         manager(bufferList, eventLoop);
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
TEST_CASE("Manager::SyncBuffer does not sync a huge buffer even when a real server is configured",
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
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenFile(path);
    REQUIRE(buffer.Content().IsHuge());

    Client* client = nullptr;
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
TEST_CASE("Manager::SyncBuffer does not re-materialize an unchanged buffer's content on repeated calls "
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
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenFile(path);
    REQUIRE_FALSE(buffer.Content().IsHuge()); // ordinary RopeStorage path -- the bug wasn't specific to PieceTableStorage

    Client* client = nullptr;
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

TEST_CASE("Manager::NotifyBufferClosed is a no-op for a buffer that was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
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
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-didopen-immediate-test.txt");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");

    const std::string raw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["method"] == "textDocument/didOpen");
}

TEST_CASE("SyncBuffer debounces a rapid burst of edits into a single didChange with the final content", "[Lsp]") {
    // sync-debounce follow-up: the user's own reported bug, made concrete --
    // a burst of edits with no pause between them (well within
    // SyncDebounceMs() of each other) must collapse into exactly one
    // textDocument/didChange, not one per keystroke.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-sync-debounce-coalesce-test.txt");
    buffer.InsertAtPoint("a");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    for (const char c : std::string("bcdefghij")) {
        buffer.InsertAtPoint(std::string(1, c));
        manager.SyncBuffer(buffer, "test-lang"); // (re)arms the same debounce timer each time -- no send yet
    }
    // No "nothing sent yet" check here -- NoFrameArrives' own 200ms poll is
    // longer than SyncDebounceMs()'s 150ms default, so it would race
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
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-sync-debounce-separate-test.txt");
    buffer.InsertAtPoint("a");

    Client* client = nullptr;
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
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(
        std::filesystem::temp_directory_path() / "ned-lsp-manager-incremental-default-full-test.txt");
    buffer.InsertAtPoint("a");

    Client* client = nullptr;
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
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(
        std::filesystem::temp_directory_path() / "ned-lsp-manager-incremental-append-test.txt");
    buffer.InsertAtPoint("abcde");

    Client* client = nullptr;
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
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(
        std::filesystem::temp_directory_path() / "ned-lsp-manager-incremental-rangelength-test.txt");
    buffer.InsertAtPoint("caf\xc3\xa9!"); // "café!" -- é is 2 bytes UTF-8, 1 UTF-16 unit

    Client* client = nullptr;
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

TEST_CASE("SyncBuffer's incremental diff snaps a diverging multi-byte character to its codepoint boundary", "[Lsp]") {
    // Regression test for a real bug: two different 3-byte UTF-8 characters
    // that share their first two bytes (the left/right "smart quote" pair,
    // E2 80 9C vs E2 80 9D -- exactly the kind of edit prose text is full
    // of) make the byte-level common-prefix scan stop mid-codepoint.
    // Unsnapped, that produced a corrupted range whose start silently
    // defaulted to {0, 0} instead of the real position -- verified to
    // desync/crash a real Incremental-capable server (harper-ls) over a
    // long editing session. This buffer is otherwise plain ASCII so the
    // full range must be exactly the width of the one changed character.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(
        std::filesystem::temp_directory_path() / "ned-lsp-manager-incremental-utf8-boundary-test.txt");
    buffer.InsertAtPoint("abc\xe2\x80\x9c"
                         "def"); // "abc" + U+201C (“) + "def"

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetTextDocumentSyncKindForTesting("test-lang", TextDocumentSyncKind::Incremental);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    // Replace “ (U+201C, bytes 3-5) with ” (U+201D, bytes 3-5) -- differs
    // only in the final continuation byte (0x9C vs 0x9D), so the forward
    // byte scan matches through "abc" + the first two bytes of the
    // character before diverging mid-codepoint.
    buffer.SetPoint(3);
    buffer.DeleteRange(3, 3);
    buffer.InsertAtPoint("\xe2\x80\x9d");
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string raw    = ReadRawFrame(server.serverStdinRead);
    const Json        frame  = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    const Json&       change = frame["params"]["contentChanges"][0];

    REQUIRE(change["text"] == "\xe2\x80\x9d");
    REQUIRE(change["rangeLength"] == 1); // one UTF-16 unit -- the replaced BMP character
    REQUIRE(change["range"]["start"]["line"] == 0);
    REQUIRE(change["range"]["start"]["character"] == 3); // must NOT have defaulted to 0
    REQUIRE(change["range"]["end"]["line"] == 0);
    REQUIRE(change["range"]["end"]["character"] == 4);
}

TEST_CASE("SyncBuffer's incremental diff widens to the outer span across a burst of debounced edits", "[Lsp]") {
    // Mirrors "SyncBuffer debounces a rapid burst of edits..." above, but
    // with Incremental set -- a burst legitimately coalesces into one
    // didChange whose diffed span may widen across the whole burst; what
    // matters is that applying it to the old text reproduces the final
    // content, not that the span is minimal.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(
        std::filesystem::temp_directory_path() / "ned-lsp-manager-incremental-burst-test.txt");
    buffer.InsertAtPoint("a");

    Client* client = nullptr;
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
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(
        std::filesystem::temp_directory_path() / "ned-lsp-manager-incremental-none-fallback-test.txt");
    buffer.InsertAtPoint("a");

    Client* client = nullptr;
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
    // the (now-closed, from Manager's perspective) connection.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-sync-debounce-close-test.txt");
    buffer.InsertAtPoint("a");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    buffer.InsertAtPoint("b");
    manager.SyncBuffer(buffer, "test-lang");    // arms the debounce -- never allowed to fire
    manager.NotifyBufferClosed(buffer);         // must not crash; cancels the pending timer
    (void)ReadRawFrame(server.serverStdinRead); // drain didClose

    // Long enough for the (cancelled) debounce to have fired if it were
    // somehow still live -- nothing should ever arrive.
    std::this_thread::sleep_for(std::chrono::milliseconds(2 * ned::editor::lsp::SyncDebounceMs()));
    eventLoop.DrainPosted_();
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("Manager::RequestHover round-trips a real request/response through an injected client", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-hover-test.txt");

    Client* client = nullptr;
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

TEST_CASE("Manager routes two buffers under different resolved LSP roots to two distinct connections", "[Lsp]") {
    // LSP multi-root follow-up: the actual feature under test -- two buffers
    // whose configured root markers resolve to two different directories
    // (neither the process's own ProjectRoot()) must never share a
    // connection, even though both sync the exact same language/serverKey
    // string. SetClientForTesting's connectionKeyOverride pre-registers a
    // fake server under the exact connection identity SyncBuffer's real
    // resolution path (RootResolver.h's ResolveLspRoot, then
    // Manager's own ConnectionKey) is expected to compute -- see
    // ConnectionKey's own doc comment in Manager.h for the composition
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
    Manager         manager(bufferList, eventLoop);
    Buffer&            bufferA = bufferList.OpenOrCreateFile(pkgA / "file.txt");
    Buffer&            bufferB = bufferList.OpenOrCreateFile(pkgB / "file.txt");

    Client* clientA = nullptr;
    Client* clientB = nullptr;
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

TEST_CASE("Two same-language connections under different roots keep independent capability and status state", "[Lsp]") {
    // LSP multi-root remainder: every connection-scoped cache used to be
    // keyed by the plain language string, so two simultaneously running
    // servers for one language against two roots shadowed each other --
    // whichever handshaked last owned the legend/sync-kind for both, and
    // either one disconnecting wiped the other's status, capabilities and
    // per-buffer sync state. Same two-root scaffold as the routing test
    // above; this one asserts the *state* stays separate, not just the
    // request routing.
    ProjectRootGuard            rootGuard;
    const std::string           language = "lsp-manager-multiroot-state-lang";
    const std::string           marker   = "lsp-manager-multiroot-state.marker";
    const std::filesystem::path base     = std::filesystem::temp_directory_path() / "ned-lsp-manager-multiroot-state";
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
    SetProjectRoot(base);
    SetLspRootMarkers(language, {marker});

    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            bufferA = bufferList.OpenOrCreateFile(pkgA / "file.txt");
    Buffer&            bufferB = bufferList.OpenOrCreateFile(pkgB / "file.txt");

    const std::string connectionA = pkgA.string() + '\x1f' + language;
    const std::string connectionB = pkgB.string() + '\x1f' + language;

    Client* clientA = nullptr;
    Client* clientB = nullptr;
    auto       serverA = std::make_optional<FakeServer>(
        FakeServer::Create(manager, language, eventLoop, clientA, Json::object(), false, connectionA));
    FakeServer serverB = FakeServer::Create(manager, language, eventLoop, clientB, Json::object(), false, connectionB);

    manager.SyncBuffer(bufferA, language);
    manager.SyncBuffer(bufferB, language);
    (void)ReadRawFrame(serverA->serverStdinRead);
    (void)ReadRawFrame(serverB.serverStdinRead);

    REQUIRE(manager.ConnectionKeyForBuffer(bufferA, language) == connectionA);
    REQUIRE(manager.ConnectionKeyForBuffer(bufferB, language) == connectionB);

    // Distinct handshake results per connection -- the shadowing the old
    // plain-language keying made impossible to express at all.
    manager.SetTextDocumentSyncKindForTesting(connectionA, TextDocumentSyncKind::Full);
    manager.SetTextDocumentSyncKindForTesting(connectionB, TextDocumentSyncKind::Incremental);
    manager.SetSemanticTokensLegendForTesting(connectionB,
                                              SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}});
    REQUIRE(manager.TextDocumentSyncKindFor(connectionA) == TextDocumentSyncKind::Full);
    REQUIRE(manager.TextDocumentSyncKindFor(connectionB) == TextDocumentSyncKind::Incremental);
    REQUIRE_FALSE(manager.SemanticTokensLegendFor(connectionA).has_value());

    serverA.reset(); // EOF on A's pipe only -- the real disconnect path
    WaitUntil(eventLoop, [&] { return manager.StatusForLanguage(connectionA) != Manager::Status::Running; });

    REQUIRE(manager.StatusForLanguage(connectionA) == Manager::Status::Disconnected);
    REQUIRE(manager.StatusForLanguage(connectionB) == Manager::Status::Running); // untouched by A's death
    REQUIRE(manager.TextDocumentSyncKindFor(connectionB) == TextDocumentSyncKind::Incremental);
    REQUIRE(manager.SemanticTokensLegendFor(connectionB).has_value());

    // Per-buffer sync state is keyed by server key but matched on the dying
    // connection -- only bufferA's entry goes.
    const std::vector<std::string> keysA = manager.ActiveServerKeysForBuffer(bufferA);
    const std::vector<std::string> keysB = manager.ActiveServerKeysForBuffer(bufferB);
    REQUIRE(std::find(keysA.begin(), keysA.end(), language) == keysA.end());
    REQUIRE(std::find(keysB.begin(), keysB.end(), language) != keysB.end());

    SetLspRootMarkers(language, {}); // cleanup -- process-wide state
    std::filesystem::remove_all(base);
}

// lsp-workspace-folders follow-up: the two-root scaffold the tests below
// share -- creates <base>/packages/{a,b} each carrying `marker`, points
// ProjectRoot() at `base` (deliberately neither package, so neither buffer's
// resolved root collapses to it), and registers marker as language's root
// marker. Caller cleans up via SetLspRootMarkers(language, {}) and
// remove_all(base); ProjectRootGuard restores the root.
struct TwoRootFixture {
    std::string           language;
    std::filesystem::path base;
    std::filesystem::path pkgA;
    std::filesystem::path pkgB;

    explicit TwoRootFixture(std::string lang) : language(std::move(lang)), base(std::filesystem::temp_directory_path() / ("ned-" + language)),
                                                pkgA(base / "packages" / "a"), pkgB(base / "packages" / "b") {
        const std::string marker = language + ".marker";
        std::filesystem::create_directories(pkgA);
        std::filesystem::create_directories(pkgB);
        {
            std::ofstream(pkgA / marker) << "";
        }
        {
            std::ofstream(pkgB / marker) << "";
        }
        SetProjectRoot(base);
        SetLspRootMarkers(language, {marker});
    }

    ~TwoRootFixture() {
        SetLspRootMarkers(language, {}); // process-wide state
        std::error_code ignored;
        std::filesystem::remove_all(base, ignored);
    }

    TwoRootFixture(const TwoRootFixture&)            = delete;
    TwoRootFixture& operator=(const TwoRootFixture&) = delete;

    [[nodiscard]] std::string ConnectionKeyFor(const std::filesystem::path& pkg) const {
        return pkg.string() + '\x1f' + language;
    }
};

// lsp-workspace-folders follow-up: the initialize response a server that
// can serve several roots from one process sends back.
Json WorkspaceFoldersInitializeResult(bool supported, bool changeNotifications) {
    return Json{{"capabilities",
                 {{"workspace", {{"workspaceFolders", {{"supported", supported}, {"changeNotifications", changeNotifications}}}}}}}};
}

TEST_CASE("A second root joins an existing workspaceFolders-capable connection instead of spawning its own", "[Lsp]") {
    // lsp-workspace-folders follow-up: the feature itself. Buffer A spawns
    // (well, injects) a connection at pkgA; once that connection's handshake
    // reports workspaceFolders support, buffer B under pkgB must be handed
    // to the *same* client via workspace/didChangeWorkspaceFolders rather
    // than getting a second process.
    ProjectRootGuard rootGuard;
    TwoRootFixture   fixture("lsp-workspace-folders-join-lang");

    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            bufferA = bufferList.OpenOrCreateFile(fixture.pkgA / "file.txt");
    Buffer&            bufferB = bufferList.OpenOrCreateFile(fixture.pkgB / "file.txt");

    Client* clientA = nullptr;
    FakeServer serverA = FakeServer::Create(manager, fixture.language, eventLoop, clientA, Json::object(), false,
                                            fixture.ConnectionKeyFor(fixture.pkgA));
    manager.SetWorkspaceFoldersSupportForTesting(fixture.ConnectionKeyFor(fixture.pkgA),
                                                 {.supported = true, .changeNotifications = true});

    manager.SyncBuffer(bufferA, fixture.language);
    (void)ReadRawFrame(serverA.serverStdinRead); // bufferA's own didOpen

    manager.SyncBuffer(bufferB, fixture.language);

    // bufferB's traffic lands on serverA's pipe: first the folder
    // notification, then its own didOpen. Both are tiny and sent
    // back-to-back, so they routinely arrive in a single read() -- read by
    // frame count rather than with ReadRawFrame's one-frame-per-call parse.
    const std::vector<Json> frames = ParseAllFrames(ReadRawFramesUntil(serverA.serverStdinRead, 2));
    REQUIRE(frames.size() >= 2);
    REQUIRE(frames[0]["method"] == "workspace/didChangeWorkspaceFolders");
    REQUIRE(frames[0]["params"]["event"]["added"].size() == 1);
    REQUIRE(frames[0]["params"]["event"]["added"][0]["name"] == "b");
    REQUIRE(frames[0]["params"]["event"]["removed"].empty());
    REQUIRE(frames[1]["method"] == "textDocument/didOpen");

    // Both buffers now report the same connection -- pkgA's, not pkgB's own.
    REQUIRE(manager.ConnectionKeyForBuffer(bufferB, fixture.language) == fixture.ConnectionKeyFor(fixture.pkgA));
    REQUIRE(manager.ConnectionKeyForBuffer(bufferA, fixture.language) == fixture.ConnectionKeyFor(fixture.pkgA));
    REQUIRE(manager.StatusForLanguage(fixture.ConnectionKeyFor(fixture.pkgB)) == Manager::Status::NotConfigured);
}

TEST_CASE("A root does not join a connection whose server can't be told about new folders", "[Lsp]") {
    // supported:true but changeNotifications absent means the server can only
    // serve the folders it got at initialize time -- useless here, since a
    // second root is only ever discovered afterwards. Must fall back to the
    // pre-existing separate-connection behavior rather than silently
    // sending a notification the server never agreed to receive.
    ProjectRootGuard rootGuard;
    TwoRootFixture   fixture("lsp-workspace-folders-nonotify-lang");

    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            bufferA = bufferList.OpenOrCreateFile(fixture.pkgA / "file.txt");
    Buffer&            bufferB = bufferList.OpenOrCreateFile(fixture.pkgB / "file.txt");

    Client* clientA = nullptr;
    Client* clientB = nullptr;
    FakeServer serverA = FakeServer::Create(manager, fixture.language, eventLoop, clientA, Json::object(), false,
                                            fixture.ConnectionKeyFor(fixture.pkgA));
    FakeServer serverB = FakeServer::Create(manager, fixture.language, eventLoop, clientB, Json::object(), false,
                                            fixture.ConnectionKeyFor(fixture.pkgB));
    manager.SetWorkspaceFoldersSupportForTesting(fixture.ConnectionKeyFor(fixture.pkgA),
                                                 {.supported = true, .changeNotifications = false});
    manager.SetWorkspaceFoldersSupportForTesting(fixture.ConnectionKeyFor(fixture.pkgB),
                                                 {.supported = true, .changeNotifications = false});

    manager.SyncBuffer(bufferA, fixture.language);
    manager.SyncBuffer(bufferB, fixture.language);

    // Each buffer opened against its own server, and neither pipe carries a
    // folder notification.
    const std::string rawA = ReadRawFrame(serverA.serverStdinRead);
    const std::string rawB = ReadRawFrame(serverB.serverStdinRead);
    REQUIRE(Json::parse(rawA.substr(rawA.find("\r\n\r\n") + 4))["method"] == "textDocument/didOpen");
    REQUIRE(Json::parse(rawB.substr(rawB.find("\r\n\r\n") + 4))["method"] == "textDocument/didOpen");
    REQUIRE(manager.ConnectionKeyForBuffer(bufferB, fixture.language) == fixture.ConnectionKeyFor(fixture.pkgB));
}

TEST_CASE("Disabling ned/set-lsp-workspace-folders keeps a process per root", "[Lsp]") {
    ProjectRootGuard rootGuard;
    TwoRootFixture   fixture("lsp-workspace-folders-off-lang");
    SetLspWorkspaceFoldersEnabled(false);
    struct Restore {
        ~Restore() {
            SetLspWorkspaceFoldersEnabled(true);
        }
    } restore;

    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            bufferA = bufferList.OpenOrCreateFile(fixture.pkgA / "file.txt");
    Buffer&            bufferB = bufferList.OpenOrCreateFile(fixture.pkgB / "file.txt");

    Client* clientA = nullptr;
    Client* clientB = nullptr;
    FakeServer serverA = FakeServer::Create(manager, fixture.language, eventLoop, clientA, Json::object(), false,
                                            fixture.ConnectionKeyFor(fixture.pkgA));
    FakeServer serverB = FakeServer::Create(manager, fixture.language, eventLoop, clientB, Json::object(), false,
                                            fixture.ConnectionKeyFor(fixture.pkgB));
    // Both fully capable of joining -- the toggle is the only thing stopping it.
    manager.SetWorkspaceFoldersSupportForTesting(fixture.ConnectionKeyFor(fixture.pkgA),
                                                 {.supported = true, .changeNotifications = true});
    manager.SetWorkspaceFoldersSupportForTesting(fixture.ConnectionKeyFor(fixture.pkgB),
                                                 {.supported = true, .changeNotifications = true});

    manager.SyncBuffer(bufferA, fixture.language);
    manager.SyncBuffer(bufferB, fixture.language);

    const std::string rawA = ReadRawFrame(serverA.serverStdinRead);
    const std::string rawB = ReadRawFrame(serverB.serverStdinRead);
    REQUIRE(Json::parse(rawA.substr(rawA.find("\r\n\r\n") + 4))["method"] == "textDocument/didOpen");
    REQUIRE(Json::parse(rawB.substr(rawB.find("\r\n\r\n") + 4))["method"] == "textDocument/didOpen");
    REQUIRE(manager.ConnectionKeyForBuffer(bufferB, fixture.language) == fixture.ConnectionKeyFor(fixture.pkgB));
}

TEST_CASE("A joined root re-resolves after the connection it joined disconnects", "[Lsp]") {
    // The canonical connection dying must not strand every root that joined
    // it pointing at a key that no longer names a client -- each one
    // re-resolves on its next sync.
    ProjectRootGuard rootGuard;
    TwoRootFixture   fixture("lsp-workspace-folders-rejoin-lang");

    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            bufferA = bufferList.OpenOrCreateFile(fixture.pkgA / "file.txt");
    Buffer&            bufferB = bufferList.OpenOrCreateFile(fixture.pkgB / "file.txt");

    Client* clientA = nullptr;
    auto       serverA = std::make_optional<FakeServer>(FakeServer::Create(
        manager, fixture.language, eventLoop, clientA, Json::object(), false, fixture.ConnectionKeyFor(fixture.pkgA)));
    manager.SetWorkspaceFoldersSupportForTesting(fixture.ConnectionKeyFor(fixture.pkgA),
                                                 {.supported = true, .changeNotifications = true});

    manager.SyncBuffer(bufferA, fixture.language);
    (void)ReadRawFrame(serverA->serverStdinRead);
    manager.SyncBuffer(bufferB, fixture.language);
    (void)ReadRawFramesUntil(serverA->serverStdinRead, 2); // folder notification + bufferB's didOpen
    REQUIRE(manager.ConnectionKeyForBuffer(bufferB, fixture.language) == fixture.ConnectionKeyFor(fixture.pkgA));

    serverA.reset();
    WaitUntil(eventLoop, [&] {
        return manager.StatusForLanguage(fixture.ConnectionKeyFor(fixture.pkgA)) != Manager::Status::Running;
    });

    // The redirect is gone -- bufferB resolves to its own root again, free to
    // spawn or join afresh on its next sync.
    REQUIRE(manager.ConnectionKeyForBuffer(bufferB, fixture.language) == fixture.ConnectionKeyFor(fixture.pkgB));
}

TEST_CASE("Manager collapses a buffer's resolved root to the plain server key when it equals ProjectRoot()", "[Lsp]") {
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
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer =
        bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-collapse-test.txt");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "lsp-manager-collapse-test-lang", eventLoop, client);

    manager.SyncBuffer(buffer, "lsp-manager-collapse-test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["method"] == "textDocument/didOpen");
}

TEST_CASE("Manager::ExpireStaleRequests reaches an injected client's own pending request", "[Lsp]") {
    // subprocess-hang-protection follow-up: confirms the manager-level sweep
    // actually forwards to a real running client, not just Client's own
    // already-covered unit behavior.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-expire-test.txt");

    Client* client = nullptr;
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

TEST_CASE("Manager::ExpireStaleRequests survives a stale initialize request disconnecting its own client mid-sweep", "[Lsp]") {
    // reentrant-expiry-during-iteration follow-up: confirmed live via a real
    // SIGSEGV (a unique_ptr<Client> read back as garbage, inside
    // ExpireStaleRequests itself). A timed-out *initialize* request's
    // synthesized-timeout callback (SpawnClient's own lambda) calls
    // ClientDisconnected on error, which erases the client from clients_
    // synchronously -- and that can happen from inside this very client's
    // own ExpireStaleRequests(maxAge) call, while Manager::
    // ExpireStaleRequests's loop is still iterating clients_, invalidating
    // the loop's own iterator. "cat" echoes the initialize request's raw
    // bytes straight back -- no "result"/"error" key, so Client::
    // DispatchFrame never treats it as a response (see that function's own
    // id-plus-result-or-error check) and the request just stays pending
    // until the timeout below fires.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    manager.SetBrokerSocketPathOverrideForTesting(std::filesystem::temp_directory_path() / "ned-lsp-manager-test-no-broker.sock");
    ned::editor::lsp::SetLspServerCommand("hang-init-lang", {"cat"});

    Buffer& buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-hang-init-test.txt");
    manager.SyncBuffer(buffer, "hang-init-lang"); // spawns cat, sends "initialize", which never validly answers
    REQUIRE(manager.StatusForLanguage("hang-init-lang") == Manager::Status::Running);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    manager.ExpireStaleRequests(std::chrono::milliseconds(1)); // the crash used to happen here

    WaitUntil(eventLoop, [&] { return manager.StatusForLanguage("hang-init-lang") != Manager::Status::Running; });
    REQUIRE(manager.StatusForLanguage("hang-init-lang") == Manager::Status::Disconnected);

    ned::editor::lsp::SetLspServerCommand("hang-init-lang", {}); // clean up global config state for other tests
}

TEST_CASE("Manager::RequestHover resolves to nullopt on a JSON-RPC error response", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-hover-error-test.txt");

    Client* client = nullptr;
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

TEST_CASE("Manager::SyncBuffer reports a spawn failure via *lsp log* instead of throwing, "
          "and doesn't retry every frame",
          "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    // ManagerTest-broker-hermeticity follow-up: without this, ClientForLanguage's
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

TEST_CASE("Manager::StatusForLanguage reports NotConfigured, Running, and SpawnFailed", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    using ned::editor::lsp::Manager;
    // See the spawn-failure test above for why this is needed for hermeticity.
    manager.SetBrokerSocketPathOverrideForTesting(std::filesystem::temp_directory_path() / "ned-lsp-manager-test-no-broker.sock");

    REQUIRE(manager.StatusForLanguage("status-test-lang") == Manager::Status::NotConfigured);
    REQUIRE(manager.SpawnFailureDetail("status-test-lang").empty());
    REQUIRE(manager.DisconnectReason("status-test-lang").empty());

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "status-test-lang", eventLoop, client);
    REQUIRE(manager.StatusForLanguage("status-test-lang") == Manager::Status::Running);
    REQUIRE(manager.SpawnFailureDetail("status-test-lang").empty());

    ned::editor::lsp::SetLspServerCommand("status-fail-lang", {"/definitely/does/not/exist/ned-fake-lsp"});
    Buffer& buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-status-test.txt");
    manager.SyncBuffer(buffer, "status-fail-lang");
    REQUIRE(manager.StatusForLanguage("status-fail-lang") == Manager::Status::SpawnFailed);
    REQUIRE(manager.StatusForLanguage("status-test-lang") == Manager::Status::Running); // unaffected by the other language's failure
    // mode-line-lsp-status-round-3 follow-up: the spawn exception's message
    // is retained for the mode line's detail text.
    REQUIRE(manager.SpawnFailureDetail("status-fail-lang").find("ned-fake-lsp") != std::string::npos);
    REQUIRE(manager.SpawnFailureDetail("status-test-lang").empty()); // unaffected by the other language's failure

    ned::editor::lsp::SetLspServerCommand("status-fail-lang", {}); // clean up global config state for other tests
}

TEST_CASE("Manager::ClientDisconnected removes the client and updates status on a real disconnect", "[Lsp]") {
    // lsp-use-after-free follow-up: confirmed live -- a real SIGSEGV/ASan
    // heap-use-after-free from Client's own background read thread
    // Post()ing a callback that outlived the object. The fix now lives in
    // Client itself (alive_, see Client.h's own header comment and
    // ClientTest.cpp's "A stray Post()ed callback safely no-ops..." for
    // the test that actually exercises that race) rather than here --
    // ClientDisconnected went back to a plain, immediate clients_.erase()
    // once that was fixed at the source. An earlier version of this fix
    // tried deferring destruction here instead (a retired_ vector, drained
    // by a periodic tick) and was confirmed live to not actually be safe at
    // any delay -- Client's own periodic maintenance tick and a client's
    // background thread both Post() against EventLoop with no ordering
    // guarantee between them. This test just confirms the ordinary,
    // expected behavior: a real disconnect removes the client and updates
    // status, full stop.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);

    Client* client = nullptr;
    auto       server = std::make_optional<FakeServer>(FakeServer::Create(manager, "disconnect-test-lang", eventLoop, client));
    REQUIRE(client != nullptr);
    REQUIRE(manager.StatusForLanguage("disconnect-test-lang") == Manager::Status::Running);

    server.reset(); // closes the fake server's write end -- EOF, the real disconnect path
    WaitUntil(eventLoop, [&] { return manager.StatusForLanguage("disconnect-test-lang") != Manager::Status::Running; });
    REQUIRE(manager.StatusForLanguage("disconnect-test-lang") == Manager::Status::Disconnected);
}

TEST_CASE("ClientDisconnected erases the cached textDocumentSync capability, re-defaulting to Full", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);

    Client* client = nullptr;
    auto       server = std::make_optional<FakeServer>(FakeServer::Create(manager, "disconnect-sync-kind-test-lang", eventLoop, client));
    REQUIRE(client != nullptr);
    manager.SetTextDocumentSyncKindForTesting("disconnect-sync-kind-test-lang", TextDocumentSyncKind::Incremental);
    REQUIRE(manager.TextDocumentSyncKindFor("disconnect-sync-kind-test-lang") == TextDocumentSyncKind::Incremental);

    server.reset(); // closes the fake server's write end -- EOF, the real disconnect path
    WaitUntil(eventLoop, [&] {
        return manager.StatusForLanguage("disconnect-sync-kind-test-lang") != Manager::Status::Running;
    });
    REQUIRE(manager.TextDocumentSyncKindFor("disconnect-sync-kind-test-lang") == TextDocumentSyncKind::Full);
}

TEST_CASE("Manager::ClientDisconnected gives up after a burst of immediate disconnects (crash-loop guard)", "[Lsp]") {
    // crash-loop-respawn-guard follow-up: confirmed live -- a misconfigured
    // phpantom_lsp respawned thousands of times within about a second, since
    // nothing previously stood between one ClientDisconnected and the very
    // next SyncBuffer's respawn attempt. Simulates the same rapid-disconnect
    // shape (inject a client, immediately EOF it, repeat) without a real
    // subprocess at all.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    ned::editor::lsp::SetLspServerCommand("crashloop-lang", {"/definitely/does/not/exist/ned-crashloop-lsp"});

    for (int i = 0; i < 3; ++i) {
        Client* client = nullptr;
        {
            FakeServer server = FakeServer::Create(manager, "crashloop-lang", eventLoop, client);
            // FakeServer's destructor closes serverStdoutWrite here -- EOF,
            // which Client's own read loop reports as onDisconnected_.
        }
        WaitUntil(eventLoop, [&] { return manager.StatusForLanguage("crashloop-lang") != Manager::Status::Running; });
    }

    REQUIRE(manager.StatusForLanguage("crashloop-lang") == Manager::Status::SpawnFailed);
    REQUIRE(manager.SpawnFailureDetail("crashloop-lang").find("disconnects in a row") != std::string::npos);

    ned::editor::lsp::SetLspServerCommand("crashloop-lang", {}); // clean up global config state for other tests
}

TEST_CASE("Manager::RequestCompletion round-trips a real request/response through an injected client", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-manager-completion-test.txt");
    buffer.InsertAtPoint("foo");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    bool                        invoked = false;
    std::vector<CompletionItem> gotItems;
    manager.RequestCompletion(buffer, buffer.Point(), [&](ned::editor::lsp::CompletionList list) {
        invoked  = true;
        gotItems = std::move(list.items);
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

TEST_CASE("Manager routes a real publishDiagnostics notification into Buffer::Diagnostics()", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-diagnostics-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code");

    Client* client = nullptr;
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

// stale-publish-position follow-up. A server's positions are {line, character}
// against the document version it was last sent, and it answers on its own
// schedule -- so converting them against the buffer's *current* content puts
// every diagnostic on the wrong bytes until the next publish catches up.
// Reported as "the underlines still shift waiting for LSP redraw".
TEST_CASE("A publish's positions are converted against the version the server was sent", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-stale-publish-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int alpha = 1;");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen -- the server now knows "int alpha = 1;"

    // Type ahead of the word while the server is still thinking. No re-sync:
    // this is exactly the window the bug lived in.
    buffer.SetPoint(0);
    buffer.InsertAtPoint("yy");
    REQUIRE(buffer.Text() == "yyint alpha = 1;");

    // The server answers for what it was actually sent: "alpha" at columns
    // 4..9 of the *original* line.
    client->DispatchFrame(Json{
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params",
         {{"uri", "file://" + path.string()},
          {"diagnostics",
           Json::array({{{"range", {{"start", {{"line", 0}, {"character", 4}}}, {"end", {{"line", 0}, {"character", 9}}}}},
                         {"severity", 2},
                         {"message", "unused variable alpha"}}})}}},
    }
                              .dump());
    WaitForDiagnosticCount(eventLoop, buffer, 1);

    REQUIRE(buffer.Diagnostics().size() == 1);
    // The whole point: the range names "alpha" in the buffer as it is now, at
    // 6..11 -- not 4..9, which is "t alp" and is what this used to produce.
    const Buffer::Diagnostic& diagnostic = buffer.Diagnostics()[0];
    REQUIRE(buffer.Text().substr(diagnostic.startByte, diagnostic.endByte - diagnostic.startByte) == "alpha");
}

// stale-publish-position follow-up, second half. The publish above is
// converted correctly at *receipt* -- and then held for
// DiagnosticsDebounceMs() while typing carries on, and applied with the
// offsets it had on arrival. Reported as underlines that sit a couple of
// columns off the token while typing and snap back once the burst ends.
TEST_CASE("Edits made during the diagnostics debounce move the pending publish with them", "[Lsp]") {
    const int originalDebounceMs = ned::editor::lsp::DiagnosticsDebounceMs();
    ned::editor::lsp::SetLspDiagnosticsDebounceMs(300);

    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-debounce-window-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int alpha = 1;");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    // The server answers for exactly what it was sent -- correct on arrival.
    client->DispatchFrame(Json{
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params",
         {{"uri", "file://" + path.string()},
          {"diagnostics",
           Json::array({{{"range", {{"start", {{"line", 0}, {"character", 4}}}, {"end", {{"line", 0}, {"character", 9}}}}},
                         {"severity", 2},
                         {"message", "unused variable alpha"}}})}}},
    }
                              .dump());

    // Typing does not stop for the debounce. These two characters land after
    // the publish was parsed and before the timer fires.
    buffer.SetPoint(0);
    buffer.InsertAtPoint("yy");
    REQUIRE(buffer.Text() == "yyint alpha = 1;");

    WaitForDiagnosticCount(eventLoop, buffer, 1);

    REQUIRE(buffer.Diagnostics().size() == 1);
    const Buffer::Diagnostic& diagnostic = buffer.Diagnostics()[0];
    REQUIRE(buffer.Text().substr(diagnostic.startByte, diagnostic.endByte - diagnostic.startByte) == "alpha");

    ned::editor::lsp::SetLspDiagnosticsDebounceMs(originalDebounceMs);
}

// buffer-anchored-lsp-results: the third case the same debounce window
// produces and a snapshot diff could not express. Two edits either side of a
// flagged token are one contiguous changed region to a diff, so the token
// looks like it sits inside the change and the underline clamps to the edit
// point. Replaying the buffer's own edits has no such blind spot.
TEST_CASE("A pending publish survives edits on both sides of what it flags", "[Lsp]") {
    const int originalDebounceMs = ned::editor::lsp::DiagnosticsDebounceMs();
    ned::editor::lsp::SetLspDiagnosticsDebounceMs(300);

    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-two-sided-edit-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int alpha = 1;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    client->DispatchFrame(Json{
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params",
         {{"uri", "file://" + path.string()},
          {"diagnostics",
           Json::array({{{"range", {{"start", {{"line", 0}, {"character", 4}}}, {"end", {{"line", 0}, {"character", 9}}}}},
                         {"severity", 2},
                         {"message", "unused variable alpha"}}})}}},
    }
                              .dump());

    // One edit before the flagged token, one after, both inside the debounce
    // window and neither touching "alpha" itself.
    buffer.SetPoint(0);
    buffer.InsertAtPoint("yy");
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("zz");
    REQUIRE(buffer.Text() == "yyint alpha = 1;\nzz");

    WaitForDiagnosticCount(eventLoop, buffer, 1);

    REQUIRE(buffer.Diagnostics().size() == 1);
    const Buffer::Diagnostic& diagnostic = buffer.Diagnostics()[0];
    REQUIRE(buffer.Text().substr(diagnostic.startByte, diagnostic.endByte - diagnostic.startByte) == "alpha");

    ned::editor::lsp::SetLspDiagnosticsDebounceMs(originalDebounceMs);
}

TEST_CASE("A publishDiagnostics notification is not applied until the debounce delay elapses", "[Lsp]") {
    const int originalDebounceMs = ned::editor::lsp::DiagnosticsDebounceMs();
    ned::editor::lsp::SetLspDiagnosticsDebounceMs(100);

    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-diagnostics-debounce-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code");

    Client* client = nullptr;
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
    const int originalDebounceMs = ned::editor::lsp::DiagnosticsDebounceMs();
    ned::editor::lsp::SetLspDiagnosticsDebounceMs(150);

    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-diagnostics-burst-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code bad code bad code");

    Client* client = nullptr;
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

TEST_CASE("Manager::RequestCodeActions sends the range and overlapping diagnostics, and round-trips a real response",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-code-action-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code");
    buffer.SetDiagnostics({
        Buffer::Diagnostic{.startByte = 0, .endByte = 3, .severity = Buffer::Diagnostic::Severity::Error, .message = "boom"},
    });

    Client* client = nullptr;
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

TEST_CASE("Manager::RequestCodeActions resolves to an empty list when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
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

TEST_CASE("Manager::ResolveCodeAction sends action.raw verbatim and returns the resolved edit", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-resolve-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code");

    Client* client = nullptr;
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

TEST_CASE("Manager::ResolveCodeActionEdits resolves a URI per touched file", "[Lsp]") {
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

    const auto resolved = Manager::ResolveCodeActionEdits(action);
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->size() == 2);
    REQUIRE((*resolved)[0].path == std::filesystem::path("/a.c"));
    REQUIRE((*resolved)[0].edits.size() == 1);
    REQUIRE((*resolved)[0].edits[0].newText == "x");
    REQUIRE((*resolved)[1].path == std::filesystem::path("/b.c"));
}

TEST_CASE("Manager::ResolveCodeActionEdits returns nullopt for touchesUnsupportedForm or a missing edit", "[Lsp]") {
    CodeAction unsupported;
    unsupported.hasEdit                = true;
    unsupported.touchesUnsupportedForm = true;
    REQUIRE_FALSE(Manager::ResolveCodeActionEdits(unsupported).has_value());

    CodeAction noEdit;
    noEdit.hasEdit = false;
    REQUIRE_FALSE(Manager::ResolveCodeActionEdits(noEdit).has_value());
}

TEST_CASE("Manager::ResolveCodeActionEdits refuses wholesale when one URI doesn't resolve", "[Lsp]") {
    CodeAction action;
    action.hasEdit = true;
    action.edits   = {
        ned::editor::lsp::RenameEdit{.uri = "file:///a.c", .edits = {ned::editor::lsp::WorkspaceTextEdit{.newText = "x"}}},
        ned::editor::lsp::RenameEdit{.uri = "not-a-file-uri", .edits = {ned::editor::lsp::WorkspaceTextEdit{.newText = "y"}}},
    };

    REQUIRE_FALSE(Manager::ResolveCodeActionEdits(action).has_value());
}

TEST_CASE("Manager::ResolveCodeAction resolves to nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
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

TEST_CASE("Manager::RequestCodeActions with a serverKey routes to that connection, not the primary one", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-code-action-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("teh");

    Client* primaryClient = nullptr;
    Client* proseClient   = nullptr;
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

TEST_CASE("Manager::ExecuteCommand sends workspace/executeCommand and reports ok on a real response", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-execute-command-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    Client* client = nullptr;
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

TEST_CASE("Manager::ExecuteCommand reports failure on a JSON-RPC error response", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-execute-command-error-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    Client* client = nullptr;
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

TEST_CASE("Manager::ExecuteCommand reports failure when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
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

TEST_CASE("Manager::RequestDefinition resolves a Location-array response's uris to real paths", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-definition-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                      invoked = false;
    std::vector<Manager::ResolvedLocation> got;
    manager.RequestDefinition(buffer, 0, [&](std::vector<Manager::ResolvedLocation> locations) {
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

TEST_CASE("Manager::RequestDefinition resolves to an empty list when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                      invoked = false;
    std::vector<Manager::ResolvedLocation> got;
    manager.RequestDefinition(buffer, 0, [&](std::vector<Manager::ResolvedLocation> locations) {
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
TEST_CASE("Manager::RequestDeclaration sends textDocument/declaration and resolves a response", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-declaration-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                      invoked = false;
    std::vector<Manager::ResolvedLocation> got;
    manager.RequestDeclaration(buffer, 0, [&](std::vector<Manager::ResolvedLocation> locations) {
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

TEST_CASE("Manager::RequestTypeDefinition sends textDocument/typeDefinition", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-typedefinition-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    manager.RequestTypeDefinition(buffer, 0, [](std::vector<Manager::ResolvedLocation>) {});

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/typeDefinition");
}

TEST_CASE("Manager::RequestImplementation sends textDocument/implementation", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-implementation-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    manager.RequestImplementation(buffer, 0, [](std::vector<Manager::ResolvedLocation>) {});

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/implementation");
}

// find-references follow-up: same "sends its own distinct wire method"
// shape as the three tests above, plus the one thing that's actually unique
// to this request -- a "context": {"includeDeclaration": true} field none
// of the other three location requests send.
TEST_CASE("Manager::RequestReferences sends textDocument/references with includeDeclaration and resolves a response",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-references-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                      invoked = false;
    std::vector<Manager::ResolvedLocation> got;
    manager.RequestReferences(buffer, 0, [&](std::vector<Manager::ResolvedLocation> locations) {
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
TEST_CASE("Manager::RequestDocumentSymbols sends textDocument/documentSymbol and resolves its own uri to a path",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-docsymbol-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("struct Widget {};");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                  invoked = false;
    std::vector<Manager::SymbolResult> got;
    manager.RequestDocumentSymbols(buffer, [&](std::vector<Manager::SymbolResult> symbols) {
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

TEST_CASE("Manager::RequestWorkspaceSymbols sends workspace/symbol with the query and no textDocument field",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-wssymbol-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("x");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    bool                                  invoked = false;
    std::vector<Manager::SymbolResult> got;
    manager.RequestWorkspaceSymbols(buffer, "Wid", [&](std::vector<Manager::SymbolResult> symbols) {
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

TEST_CASE("Manager::RequestPrepareCallHierarchy sends textDocument/prepareCallHierarchy and resolves the item's uri "
          "to a path",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prepare-callh-test.cpp";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site();");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    const std::string didOpen = ReadRawFrame(server.serverStdinRead);
    const std::string ownUri  = Json::parse(didOpen.substr(didOpen.find("\r\n\r\n") + 4))["params"]["textDocument"]["uri"].get<std::string>();

    bool                                           invoked = false;
    std::vector<Manager::ResolvedHierarchyItem> got;
    manager.RequestPrepareCallHierarchy(buffer, 0, [&](std::vector<Manager::ResolvedHierarchyItem> items) {
        invoked = true;
        got     = std::move(items);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/prepareCallHierarchy");
    REQUIRE(request["params"]["textDocument"]["uri"] == ownUri);
    REQUIRE(request["params"]["position"]["line"] == 0);

    const Json itemJson = {
        {"name", "call_site"},
        {"kind", 12},
        {"uri", ownUri},
        {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 9}}}}},
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

TEST_CASE("Manager::RequestIncomingCalls sends item.raw verbatim as \"item\" and resolves fromRanges/from.uri",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-incoming-calls-test.cpp";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("callee();");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    const std::string didOpen = ReadRawFrame(server.serverStdinRead);
    const std::string ownUri  = Json::parse(didOpen.substr(didOpen.find("\r\n\r\n") + 4))["params"]["textDocument"]["uri"].get<std::string>();

    const Json                            requestedItem = {{"name", "callee"},
                                                           {"kind", 12},
                                                           {"uri", ownUri},
                                                           {"selectionRange", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 6}}}}},
                                                           {"data", {{"opaque", 2}}}};
    const ned::editor::lsp::HierarchyItem item          = ned::editor::lsp::ExtractHierarchyItems(Json::array({requestedItem}))[0];

    bool                                           invoked = false;
    std::vector<Manager::ResolvedHierarchyCall> got;
    manager.RequestIncomingCalls(buffer, item, [&](std::vector<Manager::ResolvedHierarchyCall> calls) {
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

TEST_CASE("Manager::RequestSupertypes sends typeHierarchy/supertypes with item.raw and resolves the response",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-supertypes-test.cpp";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("class Derived {};");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    const std::string didOpen = ReadRawFrame(server.serverStdinRead);
    const std::string ownUri  = Json::parse(didOpen.substr(didOpen.find("\r\n\r\n") + 4))["params"]["textDocument"]["uri"].get<std::string>();

    const Json                            requestedItem = {{"name", "Derived"},
                                                           {"kind", 5},
                                                           {"uri", ownUri},
                                                           {"selectionRange", {{"start", {{"line", 0}, {"character", 6}}}, {"end", {{"line", 0}, {"character", 13}}}}}};
    const ned::editor::lsp::HierarchyItem item          = ned::editor::lsp::ExtractHierarchyItems(Json::array({requestedItem}))[0];

    bool                                           invoked = false;
    std::vector<Manager::ResolvedHierarchyItem> got;
    manager.RequestSupertypes(buffer, item, [&](std::vector<Manager::ResolvedHierarchyItem> items) {
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

TEST_CASE("Manager::RequestPrepareCallHierarchy resolves an empty vector when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                           invoked = false;
    std::vector<Manager::ResolvedHierarchyItem> got{Manager::ResolvedHierarchyItem{}}; // pre-seeded, must be cleared
    manager.RequestPrepareCallHierarchy(buffer, 0, [&](std::vector<Manager::ResolvedHierarchyItem> items) {
        invoked = true;
        got     = std::move(items);
    });

    REQUIRE(invoked);
    REQUIRE(got.empty());
}

TEST_CASE("Manager::RequestSignatureHelp sends textDocument/signatureHelp and resolves the formatted text", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-signature-help-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("foo(");

    Client* client = nullptr;
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

TEST_CASE("Manager::RequestSignatureHelp resolves nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
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

TEST_CASE("Manager::RequestDocumentHighlight sends textDocument/documentHighlight and resolves the parsed ranges", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-document-highlight-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("foo = foo + 1");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                             invoked = false;
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

TEST_CASE("Manager::RequestDocumentHighlight resolves an empty vector when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                             invoked = false;
    std::vector<ned::editor::lsp::DocumentHighlight> got;
    manager.RequestDocumentHighlight(buffer, 0, [&](std::vector<ned::editor::lsp::DocumentHighlight> highlights) {
        invoked = true;
        got     = std::move(highlights);
    });

    REQUIRE(invoked);
    REQUIRE(got.empty());
}

TEST_CASE("Manager::RequestFormatting sends textDocument/formatting with tabSize/insertSpaces and resolves edits", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-formatting-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x=1;");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                                            invoked = false;
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

TEST_CASE("Manager::RequestFormatting resolves nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                                            invoked = false;
    std::optional<std::vector<ned::editor::lsp::WorkspaceTextEdit>> got;
    manager.RequestFormatting(buffer, [&](std::optional<std::vector<ned::editor::lsp::WorkspaceTextEdit>> edits) {
        invoked = true;
        got     = std::move(edits);
    });

    REQUIRE(invoked);
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("Manager::RequestRangeFormatting sends textDocument/rangeFormatting with the requested range", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-range-formatting-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x=1;\nint y=2;");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                                            invoked = false;
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

TEST_CASE("Manager::RequestSwitchSourceHeader sends a bare TextDocumentIdentifier and resolves a string uri response",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-switch-header-test.cpp";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    Client* client = nullptr;
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

TEST_CASE("Manager::RequestSwitchSourceHeader resolves to nullopt on a null result (no counterpart)", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-switch-header-null-test.cpp";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    Client* client = nullptr;
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

TEST_CASE("Manager::RequestSwitchSourceHeader resolves to nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
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

TEST_CASE("Manager::RequestRename sends newName and resolves a multi-file WorkspaceEdit's uris to real paths", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-rename-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("old_name");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    const std::string didOpen = ReadRawFrame(server.serverStdinRead);
    const std::string ownUri  = Json::parse(didOpen.substr(didOpen.find("\r\n\r\n") + 4))["params"]["textDocument"]["uri"].get<std::string>();

    bool                                      invoked = false;
    std::optional<Manager::ResolvedRename> got;
    manager.RequestRename(buffer, 0, "new_name", [&](std::optional<Manager::ResolvedRename> result) {
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

TEST_CASE("Manager::RequestWillRenameFiles sends oldUri/newUri only to a server whose filter matches, and resolves its WorkspaceEdit",
          "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    manager.SetFileOperationFiltersForTesting("test-lang", {.willRenameGlobs = {"**/*.ts"}, .didRenameGlobs = {}});

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    const std::filesystem::path oldPath = std::filesystem::temp_directory_path() / "ned-will-rename-old.ts";
    const std::filesystem::path newPath = std::filesystem::temp_directory_path() / "ned-will-rename-new.ts";

    bool                                      invoked = false;
    std::optional<Manager::ResolvedRename> got;
    manager.RequestWillRenameFiles({Manager::FileRenameEntry{.oldPath = oldPath, .newPath = newPath}},
                                   [&](std::optional<Manager::ResolvedRename> result) {
                                       invoked = true;
                                       got     = std::move(result);
                                   });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "workspace/willRenameFiles");
    REQUIRE(request["params"]["files"].size() == 1);
    const std::string oldUri = request["params"]["files"][0]["oldUri"].get<std::string>();
    const std::string newUri = request["params"]["files"][0]["newUri"].get<std::string>();
    REQUIRE(oldUri.find(oldPath.filename().string()) != std::string::npos);
    REQUIRE(newUri.find(newPath.filename().string()) != std::string::npos);

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result",
         {{"changes",
           {{oldUri, Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}},
                                   {"newText", "x"}}})}}}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.has_value());
    REQUIRE(got->hasEdit);
    REQUIRE(got->edits.size() == 1);
}

TEST_CASE("Manager::RequestWillRenameFiles resolves to nullopt synchronously when no connected server's filter matches", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    manager.SetFileOperationFiltersForTesting("test-lang", {.willRenameGlobs = {"**/*.rs"}, .didRenameGlobs = {}});

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    bool                                      invoked = false;
    std::optional<Manager::ResolvedRename> got;
    manager.RequestWillRenameFiles(
        {Manager::FileRenameEntry{.oldPath = "/tmp/a.ts", .newPath = "/tmp/b.ts"}},
        [&](std::optional<Manager::ResolvedRename> result) {
            invoked = true;
            got     = std::move(result);
        });

    REQUIRE(invoked); // no matching server -- resolved synchronously, no frame sent
    REQUIRE_FALSE(got.has_value());
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("Manager::RequestWillRenameFiles merges edits from every matching server", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    manager.SetFileOperationFiltersForTesting("lang-a", {.willRenameGlobs = {"**/*.ts"}, .didRenameGlobs = {}});
    manager.SetFileOperationFiltersForTesting("lang-b", {.willRenameGlobs = {"**/*.ts"}, .didRenameGlobs = {}});

    Client* clientA = nullptr;
    Client* clientB = nullptr;
    FakeServer serverA = FakeServer::Create(manager, "lang-a", eventLoop, clientA);
    FakeServer serverB = FakeServer::Create(manager, "lang-b", eventLoop, clientB);

    const std::filesystem::path oldPath = "/tmp/ned-will-rename-multi-old.ts";
    const std::filesystem::path newPath = "/tmp/ned-will-rename-multi-new.ts";

    bool                                      invoked = false;
    std::optional<Manager::ResolvedRename> got;
    manager.RequestWillRenameFiles({Manager::FileRenameEntry{.oldPath = oldPath, .newPath = newPath}},
                                   [&](std::optional<Manager::ResolvedRename> result) {
                                       invoked = true;
                                       got     = std::move(result);
                                   });

    const std::string rawA = ReadRawFrame(serverA.serverStdinRead);
    const std::string rawB = ReadRawFrame(serverB.serverStdinRead);
    const std::string uriA = Json::parse(rawA.substr(rawA.find("\r\n\r\n") + 4))["params"]["files"][0]["oldUri"].get<std::string>();
    const std::string uriB = Json::parse(rawB.substr(rawB.find("\r\n\r\n") + 4))["params"]["files"][0]["oldUri"].get<std::string>();

    const auto responseWith = [](int id, const std::string& uri, const std::string& newText) {
        return Json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result",
             {{"changes",
               {{uri, Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}},
                                    {"newText", newText}}})}}}}},
        };
    };
    clientA->DispatchFrame(responseWith(RequestIdFromFrame(rawA), uriA, "a").dump());
    REQUIRE_FALSE(invoked); // one of two servers has answered -- still waiting on the other
    clientB->DispatchFrame(responseWith(RequestIdFromFrame(rawB), uriB, "b").dump());

    REQUIRE(invoked);
    REQUIRE(got.has_value());
    REQUIRE(got->hasEdit);
    REQUIRE(got->edits.size() == 2);
}

TEST_CASE("Manager::NotifyFilesRenamed sends workspace/didRenameFiles only to a server whose filter matches", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    manager.SetFileOperationFiltersForTesting("ts-lang", {.willRenameGlobs = {}, .didRenameGlobs = {"**/*.ts"}});
    manager.SetFileOperationFiltersForTesting("rs-lang", {.willRenameGlobs = {}, .didRenameGlobs = {"**/*.rs"}});

    Client* tsClient = nullptr;
    Client* rsClient = nullptr;
    FakeServer tsServer = FakeServer::Create(manager, "ts-lang", eventLoop, tsClient);
    FakeServer rsServer = FakeServer::Create(manager, "rs-lang", eventLoop, rsClient);

    manager.NotifyFilesRenamed(
        {Manager::FileRenameEntry{.oldPath = "/tmp/ned-did-rename-old.ts", .newPath = "/tmp/ned-did-rename-new.ts"}});

    const std::string raw     = ReadRawFrame(tsServer.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "workspace/didRenameFiles");
    REQUIRE_FALSE(request.contains("id")); // a notification, not a request

    REQUIRE(NoFrameArrives(rsServer.serverStdinRead));
}

TEST_CASE("Manager::RequestRename resolves to nullopt when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool                                      invoked = false;
    std::optional<Manager::ResolvedRename> got;
    manager.RequestRename(buffer, 0, "new_name", [&](std::optional<Manager::ResolvedRename> result) {
        invoked = true;
        got     = result;
    });

    REQUIRE(invoked);
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("BuildInitializeParams advertises workspaceFolders and sends the root as the first folder", "[Lsp]") {
    const Json params = ned::editor::lsp::BuildInitializeParams("/tmp/ned-workspace-folders-test");
    REQUIRE(params["capabilities"]["workspace"]["workspaceFolders"] == true);
    REQUIRE(params["workspaceFolders"].is_array());
    REQUIRE(params["workspaceFolders"].size() == 1);
    REQUIRE(params["workspaceFolders"][0]["name"] == "ned-workspace-folders-test");
    REQUIRE(params["workspaceFolders"][0]["uri"] == params["rootUri"]); // same root, both spellings
}

TEST_CASE("BuildInitializeParams sends a null workspaceFolders for an empty root", "[Lsp]") {
    // "no folders open" (an empty array) is a different claim than "this
    // client has no root," which is what an empty path means here.
    const Json params = ned::editor::lsp::BuildInitializeParams("");
    REQUIRE(params["workspaceFolders"].is_null());
    REQUIRE(params["rootUri"].is_null());
}

TEST_CASE("BuildInitializeParams advertises codeActionLiteralSupport alongside the resolve capabilities", "[Lsp]") {
    // Regression test: without codeActionLiteralSupport a spec-following
    // server (clangd included) may only return bare Command objects -- no
    // "edit", no "kind" -- so every "fix available" quickfix listed fine but
    // applied as "has no edit to apply". See BuildInitializeParams' own
    // comment in Manager.cpp.
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
    // edit-application-gaps follow-up: declares support for a server-pushed
    // workspace/applyEdit request and the richer "documentChanges"
    // WorkspaceEdit form (file create/rename/delete).
    REQUIRE(workspace.at("applyEdit") == true);
    REQUIRE(workspace.at("workspaceEdit").at("documentChanges") == true);
}

TEST_CASE("BuildInitializeParams sends booleans, not objects, for fileOperations client capabilities", "[Lsp]") {
    // Real live bug: harper-ls's initialize handshake failed outright with
    // "invalid type: map, expected a boolean" (confirmed against a real
    // harper-ls 1.8.0 process fed this exact params object). Per spec,
    // FileOperationClientCapabilities' willRename/didRename/etc. fields are
    // plain booleans, not objects -- clangd tolerated the previous {} shape
    // silently, harper-ls's serde-based parser rejects it and crash-loops.
    const Json params = ned::editor::lsp::BuildInitializeParams(std::filesystem::path("/some/project"));

    const Json& fileOperations = params.at("capabilities").at("workspace").at("fileOperations");
    REQUIRE(fileOperations.at("willRename") == true);
    REQUIRE(fileOperations.at("didRename") == true);
}

TEST_CASE("BuildInitializeParams absolutizes a relative rootUri", "[Lsp]") {
    // PathToUri (file-local in Manager.cpp, reached through
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

TEST_CASE("Manager tracks $/progress begin/report/end as LSP background activity with detail", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);

    Client* client = nullptr;
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

TEST_CASE("Manager answers window/workDoneProgress/create with a null result", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    const Json request = {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "window/workDoneProgress/create"}, {"params", {{"token", "t"}}}};
    client->DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["id"] == 3);
    REQUIRE(response.contains("result"));
    REQUIRE(response["result"].is_null());
}

TEST_CASE("Manager resolves workspace/configuration sections against lspWorkspaceConfiguration", "[Lsp]") {
    // project-settings-lsp-init-options follow-up: a config-pull server
    // (e.g. intelephense/phpactor-style) asks for its own section by dotted
    // path -- confirm both a top-level and a nested section resolve, and an
    // unconfigured one still falls back to null rather than erroring.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);

    const Json workspaceConfig = Json{{"phpactor", {{"file_extensions", Json::array({"php"})}}},
                                      {"intelephense", {{"environment", {{"includePaths", Json::array({"/stubs"})}}}}}};

    Client* client = nullptr;
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

TEST_CASE("Manager answers workspace/configuration with null for every item when nothing is configured", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    const Json request = {{"jsonrpc", "2.0"}, {"id", 9}, {"method", "workspace/configuration"}, {"params", {{"items", Json::array({{{"section", "anything"}}})}}}};
    client->DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["result"].is_array());
    REQUIRE(response["result"].size() == 1);
    CHECK(response["result"][0].is_null());
}

// edit-application-gaps follow-up: a server-pushed workspace/applyEdit
// request -- Manager::WireNotificationHandlers' own handler, exercised
// the same live request/response round-trip window/workDoneProgress/create
// and workspace/configuration already are above.
TEST_CASE("Manager answers workspace/applyEdit with applied:false when no handler is wired up", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    const Json request = {{"jsonrpc", "2.0"},
                          {"id", 11},
                          {"method", "workspace/applyEdit"},
                          {"params", {{"edit", {{"changes", {{"file:///a.c", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}}, {"newText", "x"}}})}}}}}}}};
    client->DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["id"] == 11);
    REQUIRE(response["result"]["applied"] == false);
    REQUIRE(response["result"]["failureReason"] == "not supported");
}

TEST_CASE("Manager routes workspace/applyEdit through the wired handler, resolved to real paths", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);

    std::optional<Manager::ResolvedRename> gotEdit;
    std::string                               gotLabel;
    manager.SetApplyEditHandler([&](const Manager::ResolvedRename& edit, const std::string& label) {
        gotEdit  = edit;
        gotLabel = label;
        return true;
    });

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    const Json request = {
        {"jsonrpc", "2.0"},
        {"id", 12},
        {"method", "workspace/applyEdit"},
        {"params",
         {{"label", "Rename symbol"},
          {"edit", {{"changes", {{"file:///a.c", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}}, {"newText", "x"}}})}}}}}}}};
    client->DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["result"]["applied"] == true);

    REQUIRE(gotEdit.has_value());
    REQUIRE(gotLabel == "Rename symbol");
    REQUIRE(gotEdit->hasEdit);
    REQUIRE(gotEdit->edits.size() == 1);
    REQUIRE(gotEdit->edits[0].path == std::filesystem::path("/a.c"));
    REQUIRE(gotEdit->edits[0].edits[0].newText == "x");
}

TEST_CASE("Manager answers workspace/applyEdit with applied:false for an unresolvable uri", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    manager.SetApplyEditHandler([](const Manager::ResolvedRename&, const std::string&) { return true; });

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    const Json request = {{"jsonrpc", "2.0"},
                          {"id", 13},
                          {"method", "workspace/applyEdit"},
                          {"params", {{"edit", {{"changes", {{"not-a-file-uri", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}}, {"newText", "x"}}})}}}}}}}};
    client->DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["result"]["applied"] == false);
    REQUIRE(response["result"]["failureReason"] == "unresolvable uri");
}

TEST_CASE("Manager::ResolveDocumentChangeOps resolves a create/rename/delete/edit sequence in order", "[Lsp]") {
    using ned::editor::lsp::DocumentChangeOp;
    using ned::editor::lsp::WorkspaceTextEdit;

    const std::vector<DocumentChangeOp> ops = {
        DocumentChangeOp{.kind = DocumentChangeOp::Kind::CreateFile, .uri = "file:///new.c", .overwrite = true},
        DocumentChangeOp{.kind = DocumentChangeOp::Kind::EditFile, .uri = "file:///new.c", .edits = {WorkspaceTextEdit{.newText = "x"}}},
        DocumentChangeOp{.kind = DocumentChangeOp::Kind::RenameFile, .uri = "file:///renamed.c", .oldUri = "file:///old.c"},
        DocumentChangeOp{.kind = DocumentChangeOp::Kind::DeleteFile, .uri = "file:///gone.c", .ignoreIfNotExists = true},
    };

    const auto resolved = Manager::ResolveDocumentChangeOps(ops);
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->size() == 4);
    CHECK((*resolved)[0].path == std::filesystem::path("/new.c"));
    CHECK((*resolved)[0].overwrite);
    CHECK((*resolved)[1].edits.size() == 1);
    CHECK((*resolved)[2].oldPath == std::filesystem::path("/old.c"));
    CHECK((*resolved)[2].path == std::filesystem::path("/renamed.c"));
    CHECK((*resolved)[3].ignoreIfNotExists);
}

TEST_CASE("Manager::ResolveDocumentChangeOps refuses wholesale when a rename's oldUri doesn't resolve", "[Lsp]") {
    using ned::editor::lsp::DocumentChangeOp;

    const std::vector<DocumentChangeOp> ops = {
        DocumentChangeOp{.kind = DocumentChangeOp::Kind::RenameFile, .uri = "file:///renamed.c", .oldUri = "not-a-file-uri"},
    };
    REQUIRE_FALSE(Manager::ResolveDocumentChangeOps(ops).has_value());
}

// prose-checking follow-up: the prose-checker connection is just another
// entry in the same clients_/bufferState_ maps under
// Manager::kProseLanguageKey (see Manager.h's own doc comment on that
// constant) -- SetClientForTesting works on it exactly like any other
// language, so these tests never touch ProseChecker.h's real
// auto-detect/enabled machinery at all (ProseCheckerTestGuard.cpp disables
// that globally for the whole ned_tests binary regardless).

TEST_CASE("SyncBuffer opens both the primary language server and the prose checker independently", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-sync-test.md";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("some text");

    Client* primaryClient = nullptr;
    Client* proseClient   = nullptr;
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
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-merge-test.md";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code teh");

    Client* primaryClient = nullptr;
    Client* proseClient   = nullptr;
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

// prose-check-composer follow-up (ROADMAP "Prose-check the ACP composer").
TEST_CASE("CheckComposerProseText syncs text to the prose checker with no real Buffer, and diagnostics come back via the callback",
          "[Lsp]") {
    const int originalDebounceMs = ned::editor::lsp::DiagnosticsDebounceMs();
    ned::editor::lsp::SetLspDiagnosticsDebounceMs(50);

    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);

    Client* proseClient = nullptr;
    FakeServer proseServer = FakeServer::Create(manager, std::string(kProseLanguageKey), eventLoop, proseClient);

    std::optional<std::vector<Buffer::Diagnostic>> received;
    manager.CheckComposerProseText("This have a typo.", [&](std::vector<Buffer::Diagnostic> diagnostics) { received = std::move(diagnostics); });

    // The send itself is debounced (CheckComposerProseText's own doc
    // comment), and the DeadlineTimer's fire is Post()ed onto eventLoop from
    // a background thread (DeadlineTimer's own doc comment) -- WaitUntil is
    // what actually drains that post, the same idiom WaitForDiagnosticCount
    // uses for the real-Buffer publish path.
    WaitUntil(eventLoop, [&] {
        pollfd pfd{.fd = proseServer.serverStdinRead, .events = POLLIN, .revents = 0};
        return ::poll(&pfd, 1, 0) > 0;
    });
    const std::string openRaw  = ReadRawFrame(proseServer.serverStdinRead);
    const Json        openJson = Json::parse(openRaw.substr(openRaw.find("\r\n\r\n") + 4));
    REQUIRE(openJson["method"] == "textDocument/didOpen");
    REQUIRE(openJson["params"]["textDocument"]["languageId"] == "plaintext");
    REQUIRE(openJson["params"]["textDocument"]["text"] == "This have a typo.");
    const std::string uri = openJson["params"]["textDocument"]["uri"].get<std::string>();

    // No real Buffer was ever created for this -- the whole point of this
    // API existing (AcpPanel's composer has no Buffer of its own).
    REQUIRE(bufferList.Count() == 0);

    const Json publish = {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params",
         {{"uri", uri},
          {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 5}}}, {"end", {{"line", 0}, {"character", 9}}}}},
                                        {"severity", 4},
                                        {"message", "possible typo: have"}}})}}},
    };
    proseClient->DispatchFrame(publish.dump());
    WaitUntil(eventLoop, [&] { return received.has_value(); });

    REQUIRE(received.has_value());
    REQUIRE(received->size() == 1);
    REQUIRE((*received)[0].message == "possible typo: have");
    REQUIRE((*received)[0].origin == Buffer::Diagnostic::Origin::Prose);
    REQUIRE((*received)[0].startByte == 5);
    REQUIRE((*received)[0].endByte == 9);

    ned::editor::lsp::SetLspDiagnosticsDebounceMs(originalDebounceMs);
}

TEST_CASE("A second publish from one source replaces only that source's own diagnostics slice", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-reslice-test.md";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code teh");

    Client* primaryClient = nullptr;
    Client* proseClient   = nullptr;
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

// debounce-window-drift follow-up, the cross-source half of the same bug.
// PushMergedDiagnostics rebuilds the whole set from every source's stored
// slice, so a source that has not published in a while has its offsets
// re-applied verbatim -- undoing the relocation Buffer did for it in the
// meantime -- the moment any *other* source fires.
TEST_CASE("A quiet source's diagnostics stay on their token when another source republishes after edits", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-cross-source-drift-test.md";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code teh");

    Client*    primaryClient = nullptr;
    Client*    proseClient   = nullptr;
    FakeServer primaryServer = FakeServer::Create(manager, "markdown", eventLoop, primaryClient);
    FakeServer proseServer   = FakeServer::Create(manager, std::string(ned::editor::lsp::kProseLanguageKey), eventLoop, proseClient);
    manager.SyncBuffer(buffer, "markdown");
    (void)ReadRawFrame(primaryServer.serverStdinRead);
    (void)ReadRawFrame(proseServer.serverStdinRead);

    const std::string uri          = "file://" + path.string();
    auto              publishRange = [&](const std::string& message, int startChar, int endChar) {
        return Json{
            {"jsonrpc", "2.0"},
            {"method", "textDocument/publishDiagnostics"},
            {"params",
             {{"uri", uri},
              {"diagnostics",
               Json::array({{{"range", {{"start", {{"line", 0}, {"character", startChar}}}, {"end", {{"line", 0}, {"character", endChar}}}}},
                             {"severity", 2},
                             {"message", message}}})}}},
        };
    };

    // The prose checker flags "teh" at 9..12 and then goes quiet.
    proseClient->DispatchFrame(publishRange("possible typo: teh", 9, 12).dump());
    WaitForDiagnosticCount(eventLoop, buffer, 1);
    REQUIRE(buffer.Diagnostics().size() == 1);

    // Editing ahead of it relocates it in the Buffer, which is the behaviour
    // that must survive the next merge.
    buffer.SetPoint(0);
    buffer.InsertAtPoint("zz ");
    REQUIRE(buffer.Text() == "zz bad_code teh");

    // The primary server publishes against what it was sent, naming
    // "bad_code" at 0..8, and its push rebuilds the merged set from scratch.
    primaryClient->DispatchFrame(publishRange("first error", 0, 8).dump());
    WaitUntil(eventLoop, [&] {
        return std::any_of(buffer.Diagnostics().begin(), buffer.Diagnostics().end(),
                           [](const Buffer::Diagnostic& d) { return d.message == "first error"; });
    });

    REQUIRE(buffer.Diagnostics().size() == 2);
    for (const Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        const std::string named = buffer.Text().substr(diagnostic.startByte, diagnostic.endByte - diagnostic.startByte);
        if (diagnostic.message == "possible typo: teh") {
            REQUIRE(named == "teh"); // not "ode", which is where 9..12 now points
        }
        else {
            REQUIRE(named == "bad_code");
        }
    }
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
    Manager                        manager(bufferList, eventLoop);
    const std::filesystem::path       path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-pull-diagnostics-test.txt";
    Buffer&                           buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");

    // didOpen and the pull-diagnostics request are sent back-to-back,
    // synchronously -- both can land in the same read() (ReadRawFrame's own
    // one-frame-per-call assumption breaks here, same as ParseAllFrames'
    // own header comment describes for Manager::Shutdown's frame pair).
    const std::string raw    = ReadRawFramesUntil(server.serverStdinRead, 2);
    const auto        frames = ParseAllFrames(raw);
    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0]["method"] == "textDocument/didOpen");
    REQUIRE(frames[1]["method"] == "textDocument/diagnostic");
    REQUIRE(frames[1]["params"]["textDocument"]["uri"] == "file://" + path.string());

    const auto response = Json{
        {"jsonrpc", "2.0"},
        {"id", frames[1]["id"]},
        {"result", {{"kind", "full"}, {"items", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}}, {"severity", 1}, {"message", "pulled error"}}})}}},
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
    Manager                        manager(bufferList, eventLoop);
    const std::filesystem::path       path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-pull-diagnostics-unsupported-test.txt";
    Buffer&                           buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("a");

    Client* client = nullptr;
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
    // (SyncDebounceMs), same WaitUntil-polling idiom this file already
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
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-pull-diagnostics-disabled-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen -- nothing else was ever queued behind it
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

// Live-reported 2026-09-10, right after the inline-diagnostic rows stopped
// moving and made this the visible artifact: "the colours, underlines, bolds
// and italics start wrapping weird, but the text stays where it should".
// Semantic-token spans are byte ranges resolved against the document as it
// stood when the response landed, and nothing relocates them across later
// edits -- so they recolour the wrong characters rather than moving any text.
// Code lenses are the last member of the stale-offset family, and the one with
// the loudest failure: a lens owns a whole extra screen row above the line it
// annotates, so a stale offset does not merely misplace a label -- it puts
// that row above the wrong line and everything below it moves.
TEST_CASE("Code lenses land on the right line however far the buffer has moved", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-codelens-stale-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int alpha = 1;\nint beta = 2;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestCodeLenses(buffer, 0, buffer.Content().ByteLength(), "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["method"] == "textDocument/codeLens");

    // The buffer moves on while the server is still thinking -- a whole line
    // added above both, which is the edit that actually moves a lens's row.
    buffer.SetPoint(0);
    buffer.InsertAtPoint("// header\n");

    client->DispatchFrame(Json{
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"range",
                                  {{"start", {{"line", 1}, {"character", 0}}}, {"end", {{"line", 1}, {"character", 3}}}}},
                                 {"command", {{"title", "2 references"}, {"command", "noop"}}}}})},
    }
                              .dump());

    // The server answered about line 1 of the document it was sent -- "int
    // beta". After one line was inserted above, that is line 2 here.
    REQUIRE(manager.CodeLensSpans(buffer).size() == 1);
    const auto lineOf = [&](std::size_t byteOffset) { return buffer.Content().ByteOffsetToLine(byteOffset); };
    REQUIRE(lineOf(manager.CodeLensSpans(buffer)[0].startByte) == 2);

    // And it keeps up as editing continues: another line above moves it again,
    // with no new response involved.
    buffer.SetPoint(0);
    buffer.InsertAtPoint("// second header\n");
    REQUIRE(lineOf(manager.CodeLensSpans(buffer)[0].startByte) == 3);

    // Typing *within* a line above must not move it at all -- the case that
    // already worked, pinned so the relocation cannot overshoot.
    buffer.SetPoint(0);
    buffer.InsertAtPoint("xx");
    REQUIRE(lineOf(manager.CodeLensSpans(buffer)[0].startByte) == 3);

    // Two edits either side of the lens, with no read in between -- one
    // contiguous changed region to a diff, two independent ops to the journal.
    buffer.SetPoint(0);
    buffer.InsertAtPoint("// third header\n");
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("// trailer\n");
    REQUIRE(manager.CodeLensSpans(buffer).size() == 1);
    REQUIRE(lineOf(manager.CodeLensSpans(buffer)[0].startByte) == 4);
}

// buffer-anchored-lsp-results: this used to assert that the whole set was
// withheld once the buffer moved -- the honest answer while nothing could
// relocate the spans, and one that meant the server's contribution blinked
// out on every keystroke and came back a round trip later. Carrying them
// forward through the buffer's own edits keeps them on the characters they
// describe instead; only a token whose own text an edit rewrote is dropped,
// which is the case the old blanket suppression existed to prevent.
TEST_CASE("Semantic token spans stay on the text they describe as the buffer moves", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-stale-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting("test-lang",
                                              SemanticTokensLegend{.tokenTypes = {"keyword", "variable"}, .tokenModifiers = {}});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestSemanticTokens(buffer, 0, buffer.Size(), "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", {{"data", Json::array({0, 0, 3, 0, 0, 0, 4, 1, 1, 0})}}},
    }
                              .dump());

    const auto textOf = [&](const ned::editor::HighlightSpan& span) {
        return buffer.Text().substr(span.startByte, span.endByte - span.startByte);
    };
    REQUIRE(manager.SemanticTokenSpans(buffer).size() == 2);
    REQUIRE(textOf(manager.SemanticTokenSpans(buffer)[0]) == "int");
    REQUIRE(textOf(manager.SemanticTokenSpans(buffer)[1]) == "x");

    // Type ahead of both spans: they name different bytes now, and both must
    // come with the text they colour rather than recolouring whatever moved
    // into their place.
    buffer.SetPoint(0);
    buffer.InsertAtPoint("yy");
    REQUIRE(manager.SemanticTokenSpans(buffer).size() == 2);
    REQUIRE(textOf(manager.SemanticTokenSpans(buffer)[0]) == "int");
    REQUIRE(textOf(manager.SemanticTokenSpans(buffer)[1]) == "x");

    // Two edits either side of "x", with no read in between -- the case one
    // contiguous changed region cannot express.
    buffer.SetPoint(0);
    buffer.InsertAtPoint("z");
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint(" // tail");
    REQUIRE(manager.SemanticTokenSpans(buffer).size() == 2);
    REQUIRE(textOf(manager.SemanticTokenSpans(buffer)[1]) == "x");

    // A token whose own text is rewritten is dropped, not left recolouring
    // whatever now sits there. "x" is at byte 7 of "zyyint x = 1; // tail".
    REQUIRE(buffer.Text().substr(7, 1) == "x");
    buffer.DeleteRange(6, 3); // eats " x " -- strictly containing the token
    REQUIRE(manager.SemanticTokenSpans(buffer).size() == 1);
    REQUIRE(textOf(manager.SemanticTokenSpans(buffer)[0]) == "int");
}

TEST_CASE("RequestSemanticTokens sends a plain full request when a legend is set and applies decoded, byte-resolved "
          "spans",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-tokens-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting(
        "test-lang", SemanticTokensLegend{.tokenTypes = {"keyword", "variable", "unknown"}, .tokenModifiers = {}});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    REQUIRE(manager.SemanticTokensGeneration(buffer) == 0);
    manager.RequestSemanticTokens(buffer, 0, buffer.Size(), "test-lang");
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

TEST_CASE("RequestSemanticTokens sends nothing when the server never advertised a legend", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-tokens-no-legend-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen -- nothing else was ever queued behind it

    manager.RequestSemanticTokens(buffer, 0, buffer.Size(), "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));
    REQUIRE(manager.SemanticTokenSpans(buffer).empty());
}

TEST_CASE("RequestSemanticTokens does not resend for unchanged content", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-tokens-dedup-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting("test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestSemanticTokens(buffer, 0, buffer.Size(), "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // the one real request

    // Called again with no intervening edit -- BufferView calls this once
    // per Paint(), and a cursor-blink/scroll-only repaint must not resend.
    manager.RequestSemanticTokens(buffer, 0, buffer.Size(), "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("RequestSemanticTokens sends nothing when semantic highlighting is disabled", "[Lsp]") {
    ned::editor::lsp::SetLspSemanticHighlightingEnabled(false);
    struct RestoreGuard {
        ~RestoreGuard() {
            ned::editor::lsp::SetLspSemanticHighlightingEnabled(true);
        }
    } restore;

    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-tokens-disabled-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting("test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestSemanticTokens(buffer, 0, buffer.Size(), "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("RequestSemanticTokens prefers textDocument/semanticTokens/range and asks only about ranges not already "
          "covered",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-tokens-range-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    for (int line = 0; line < 10; ++line) {
        buffer.InsertAtPoint("int x = 1;\n"); // 11 bytes a line, 110 total
    }

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting(
        "test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}, .rangeSupported = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestSemanticTokens(buffer, 0, 11, "test-lang");
    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const auto        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/semanticTokens/range");
    REQUIRE(request["params"]["range"]["start"]["line"] == 0);
    REQUIRE(request["params"]["range"]["end"]["line"] == 2); // a screenful of margin either side

    const auto response = Json{
        {"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"result", {{"data", Json::array({0, 0, 3, 0, 0})}}}};
    client->DispatchFrame(response.dump());
    REQUIRE(manager.SemanticTokenSpans(buffer).size() == 1);

    // Same (content, viewport) -- a repaint, must not resend.
    manager.RequestSemanticTokens(buffer, 0, 11, "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));

    // Scrolled a line, still inside what was asked about. This used to be a
    // resend; the margin is what makes an ordinary scroll free.
    manager.RequestSemanticTokens(buffer, 11, 22, "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));

    // Past the margin -- genuinely new ground, so one request.
    manager.RequestSemanticTokens(buffer, 44, 55, "test-lang");
    const std::string thirdRaw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(thirdRaw.substr(thirdRaw.find("\r\n\r\n") + 4))["method"] == "textDocument/semanticTokens/range");
}

// The colour half of the same bug the inlay hint retention tests cover: a
// range response describes its own slice only, so replacing the whole span
// set with it recoloured every line the response said nothing about.
TEST_CASE("A semanticTokens/range response leaves the spans outside its own range alone", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-tokens-retain-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    for (int line = 0; line < 10; ++line) {
        buffer.InsertAtPoint("int x = 1;\n");
    }

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting(
        "test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}, .rangeSupported = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestSemanticTokens(buffer, 0, 11, "test-lang");
    const std::string firstRaw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(firstRaw)},
                               {"result", {{"data", Json::array({0, 0, 3, 0, 0})}}}}
                              .dump()); // "int" on line 0
    REQUIRE(manager.SemanticTokenSpans(buffer).size() == 1);

    // Scrolled well past the first request's margin, so this is a real
    // request about ground the first response never described.
    manager.RequestSemanticTokens(buffer, 55, 66, "test-lang");
    const std::string secondRaw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(secondRaw)},
                               {"result", {{"data", Json::array({5, 0, 3, 0, 0})}}}}
                              .dump()); // "int" on line 5

    const std::vector<ned::editor::HighlightSpan>& spans = manager.SemanticTokenSpans(buffer);
    REQUIRE(spans.size() == 2);
    REQUIRE(spans[0].startByte == 0);  // line 0, kept across a scroll that never asked about it again
    REQUIRE(spans[1].startByte == 55); // line 5
}

// RequestViewportFeatures is what BufferView actually calls per frame; the
// three requests above stay public and unthrottled for callers that want one
// now. Its job is that a viewport moving every frame stops costing a round
// trip every frame, without making a discrete jump wait out a window first.
//
// Nothing here has to race the real DeadlineTimer: its fire is Post()ed onto
// eventLoop, so a deferred request doesn't leave the process until something
// drains that post, and the fire reads whatever pair is armed at drain time.
// That makes a burst of calls deterministic regardless of how the timer
// thread interleaves -- the last pair is the only one it can send.
struct RequestIdleGuard {
    explicit RequestIdleGuard(int milliseconds) {
        ned::editor::lsp::SetLspRequestIdleMs(milliseconds);
    }
    ~RequestIdleGuard() {
        ned::editor::lsp::SetLspRequestIdleMs(150);
    }
};

TEST_CASE("RequestViewportFeatures sends the first viewport at once and collapses the rest of a scroll into one",
          "[Lsp]") {
    const RequestIdleGuard      idle(200);
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-viewport-features-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;\nint y = 2;\nint z = 3;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting(
        "test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}, .rangeSupported = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    // Leading edge: the first frame at a new pair is a discrete jump as far
    // as this can tell, and goes straight out.
    manager.RequestViewportFeatures(buffer, 0, 11, "test-lang");
    const std::vector<Json> first = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 3));
    REQUIRE(first.size() == 3);
    REQUIRE(first[0]["method"] == "textDocument/semanticTokens/range");
    REQUIRE(first[0]["params"]["range"]["start"]["line"] == 0);
    REQUIRE(first[1]["method"] == "textDocument/inlayHint");
    REQUIRE(first[2]["method"] == "textDocument/codeLens");

    // The rest of the scroll, inside that window: nothing is sent from the
    // frames themselves, and the deferred fire carries the last pair only.
    manager.RequestViewportFeatures(buffer, 11, 22, "test-lang");
    manager.RequestViewportFeatures(buffer, 22, 33, "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));

    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::vector<Json> deferred = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 2));
    REQUIRE(deferred.size() >= 2);
    REQUIRE(deferred[0]["method"] == "textDocument/semanticTokens/range");
    // Line 1, not 2: both viewport-ranged requests take a screenful of
    // margin either side, so the next scroll lands on covered ground
    // (clamped to the buffer's end on the far side). Still the *last*
    // viewport's neighbourhood, never the middle one's -- which is what
    // this case is really asserting.
    REQUIRE(deferred[0]["params"]["range"]["start"]["line"] == 1);
    REQUIRE(deferred[1]["method"] == "textDocument/inlayHint");
    REQUIRE(deferred[1]["params"]["range"]["start"]["line"] == 1);

    // A further frame at the settled pair arms nothing and sends nothing.
    manager.RequestViewportFeatures(buffer, 22, 33, "test-lang");
    eventLoop.DrainPosted_();
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("RequestViewportFeatures drops an armed pair the buffer has moved off rather than asking about stale bytes",
          "[Lsp]") {
    const RequestIdleGuard      idle(1);
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-viewport-features-stale-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting(
        "test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}, .rangeSupported = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    // Nothing has told the server about this generation yet (didChange is
    // itself debounced, and only the next frame's SyncBuffer arms it), so
    // asking now would resolve the response against a document the server
    // doesn't have. Even on the leading edge, that is not sent.
    buffer.InsertAtPoint("// ");
    manager.RequestViewportFeatures(buffer, 0, 14, "test-lang");
    eventLoop.DrainPosted_();
    REQUIRE(NoFrameArrives(server.serverStdinRead));

    // The next frame: sync first, then the pair again. The dropped pair must
    // not have latched anything that suppresses the re-arm.
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string didChange = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(didChange.substr(didChange.find("\r\n\r\n") + 4))["method"] == "textDocument/didChange");

    manager.RequestViewportFeatures(buffer, 0, 14, "test-lang");
    const std::vector<Json> frames = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 1));
    REQUIRE(!frames.empty());
    REQUIRE(frames[0]["method"] == "textDocument/semanticTokens/range");
    REQUIRE(frames[0]["params"]["range"]["end"]["line"] == 1); // the post-edit viewport, converted against the post-edit text
}

TEST_CASE("RequestSemanticTokens falls back to full after a real error response to a range request, latching "
          "semanticTokensRangeUnsupported_ for the rest of the connection",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-tokens-range-unsupported-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting(
        "test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}, .rangeSupported = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestSemanticTokens(buffer, 0, buffer.Size(), "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["method"] == "textDocument/semanticTokens/range");
    client->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"error", {{"code", -32601}, {"message", "not implemented"}}}}.dump());

    // Buffer never edited (still the same content generation the failed
    // range request was for), so RequestSemanticTokens' own dedup would
    // normally no-op -- but the failure just switched this buffer onto the
    // full/delta path entirely, whose own dedup key is keyed on generation
    // alone and was never populated by the failed range attempt, so a
    // fresh call still sends the fallback request.
    manager.RequestSemanticTokens(buffer, 0, buffer.Size(), "test-lang");
    const std::string fallbackRaw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(fallbackRaw.substr(fallbackRaw.find("\r\n\r\n") + 4))["method"] == "textDocument/semanticTokens/full");
}

TEST_CASE("RequestSemanticTokens sends textDocument/semanticTokens/full/delta with previousResultId once a baseline "
          "exists, and applies the response's edits against the cached raw data",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-tokens-delta-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting(
        "test-lang", SemanticTokensLegend{.tokenTypes = {"keyword", "variable"}, .tokenModifiers = {}, .fullDeltaSupported = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    // First request: no baseline yet -- plain full, response seeds one.
    manager.RequestSemanticTokens(buffer, 0, buffer.Size(), "test-lang");
    const std::string firstRaw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(firstRaw.substr(firstRaw.find("\r\n\r\n") + 4))["method"] == "textDocument/semanticTokens/full");
    // One token: "int" (keyword, type 0) at [0,3).
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(firstRaw)},
                               {"result", {{"resultId", "v1"}, {"data", Json::array({0, 0, 3, 0, 0})}}}}
                              .dump());
    REQUIRE(manager.SemanticTokenSpans(buffer).size() == 1);

    // Content edited -- re-sync (didChange is debounced, see
    // SyncDebounceMs/SyncBuffer's own doc comment; the sibling
    // pull-diagnostics test above uses this exact WaitUntil+drain idiom) so
    // RequestSemanticTokens' own state->lastSyncedGeneration guard actually
    // lets the second request through. The second request now has a
    // baseline to delta against.
    buffer.InsertAtPoint(" ");
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string didChange = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(didChange.substr(didChange.find("\r\n\r\n") + 4))["method"] == "textDocument/didChange");

    manager.RequestSemanticTokens(buffer, 0, buffer.Size(), "test-lang");
    const std::string secondRaw     = ReadRawFrame(server.serverStdinRead);
    const auto        secondRequest = Json::parse(secondRaw.substr(secondRaw.find("\r\n\r\n") + 4));
    REQUIRE(secondRequest["method"] == "textDocument/semanticTokens/full/delta");
    REQUIRE(secondRequest["params"]["previousResultId"] == "v1");

    // Delta response: insert one more quintuple (a "variable" token, type
    // 1) after the cached one via an edit rather than resending "data".
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(secondRaw)},
                               {"result",
                                {{"resultId", "v2"},
                                 {"edits", Json::array({{{"start", 5}, {"deleteCount", 0}, {"data", Json::array({0, 5, 1, 1, 0})}}})}}}}
                              .dump());
    const std::vector<HighlightSpan>& spans = manager.SemanticTokenSpans(buffer);
    REQUIRE(spans.size() == 2);
    REQUIRE(spans[0].syntaxClass == SyntaxClass::Keyword);
    REQUIRE(spans[1].syntaxClass == SyntaxClass::Variable);
    REQUIRE(spans[1].startByte == 5);
}

TEST_CASE("RequestSemanticTokens falls back to plain full requests after a real error response to a full/delta "
          "request, latching semanticTokensFullDeltaUnsupported_",
          "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-tokens-delta-unsupported-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting(
        "test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}, .fullDeltaSupported = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestSemanticTokens(buffer, 0, buffer.Size(), "test-lang");
    const std::string firstRaw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(firstRaw)},
                               {"result", {{"resultId", "v1"}, {"data", Json::array({0, 0, 3, 0, 0})}}}}
                              .dump());

    // Re-sync after the edit -- see the sibling delta-application test's own
    // comment on why (debounced didChange, WaitUntil+drain idiom).
    buffer.InsertAtPoint(" ");
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string firstDidChange = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(firstDidChange.substr(firstDidChange.find("\r\n\r\n") + 4))["method"] == "textDocument/didChange");

    manager.RequestSemanticTokens(buffer, 0, buffer.Size(), "test-lang");
    const std::string secondRaw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(secondRaw.substr(secondRaw.find("\r\n\r\n") + 4))["method"] == "textDocument/semanticTokens/full/delta");
    client->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(secondRaw)}, {"error", {{"code", -32601}, {"message", "not implemented"}}}}.dump());

    buffer.InsertAtPoint(" ");
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::string secondDidChange = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(secondDidChange.substr(secondDidChange.find("\r\n\r\n") + 4))["method"] == "textDocument/didChange");

    manager.RequestSemanticTokens(buffer, 0, buffer.Size(), "test-lang");
    const std::string thirdRaw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(thirdRaw.substr(thirdRaw.find("\r\n\r\n") + 4))["method"] == "textDocument/semanticTokens/full");
}

TEST_CASE("RequestInlayHints sends the viewport range and applies byte-resolved, sorted hints", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    Client* client = nullptr;
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

    const std::vector<Manager::ResolvedInlayHint>& hints = manager.InlayHintSpans(buffer);
    REQUIRE(hints.size() == 2);
    REQUIRE(hints[0].byteOffset == 3);
    REQUIRE(hints[1].byteOffset == 9);
}

// code-action-hints follow-up. The gutter marker saying "a diagnostic on
// this line has a server-supplied fix", fed by one viewport-scoped
// textDocument/codeAction per settle.
TEST_CASE("RequestCodeActionHints sends only=quickfix with the viewport's own diagnostics", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-code-action-hints-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("ab = 1;\ncd = 2;\n"); // line0 = [0,8), line1 = [8,16)
    buffer.SetDiagnostics({Buffer::Diagnostic{.startByte = 0, .endByte = 2, .severity = Buffer::Diagnostic::Severity::Error, .message = "bad"}});

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestCodeActionHints(buffer, 0, buffer.Size(), "test-lang");
    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const auto        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/codeAction");
    REQUIRE(request["params"]["context"]["only"] == Json::array({"quickfix"}));
    REQUIRE(request["params"]["context"]["diagnostics"].size() == 1);

    // Two actions for the same diagnostic ("fix this"/"fix all of these"),
    // which must still light exactly one line.
    const auto response = Json{
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({
                       {{"title", "fix it"},
                        {"kind", "quickfix"},
                        {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 2}}}}}}})}},
                       {{"title", "fix all of these"},
                        {"kind", "quickfix"},
                        {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 2}}}}}}})}},
                   })},
    };
    client->DispatchFrame(response.dump());

    const std::vector<Manager::CodeActionHint>& hints = manager.CodeActionHintSpans(buffer);
    REQUIRE(hints.size() == 1);
    REQUIRE(hints[0].startByte == 0);
    REQUIRE(hints[0].endByte == 2);
}

TEST_CASE("RequestCodeActionHints asks nothing at all for a viewport with no diagnostic in it", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-code-action-hints-clean-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("ab = 1;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestCodeActionHints(buffer, 0, buffer.Size(), "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead)); // nothing is wrong here, so nothing is fixable
    REQUIRE(manager.CodeActionHintSpans(buffer).empty());
}

// The marker has to retire on its own: the user fixes the flagged line, the
// server publishes a clean set, and the next settle must clear the hint
// without a round trip to do it.
TEST_CASE("A viewport whose diagnostics have gone away clears the hints it had", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-code-action-hints-retire-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("ab = 1;\n");
    buffer.SetDiagnostics({Buffer::Diagnostic{.startByte = 0, .endByte = 2, .severity = Buffer::Diagnostic::Severity::Error, .message = "bad"}});

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestCodeActionHints(buffer, 0, buffer.Size(), "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(raw)},
                               {"result", Json::array({{{"title", "fix it"},
                                                        {"kind", "quickfix"},
                                                        {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 2}}}}}}})}}})}}
                              .dump());
    REQUIRE(manager.CodeActionHintSpans(buffer).size() == 1);

    // A clean publish: no edit behind it, so nothing but the diagnostics
    // generation has moved -- which is exactly the gate this feature needs
    // and the other viewport features do not have.
    buffer.SetDiagnostics({});
    manager.RequestCodeActionHints(buffer, 0, buffer.Size(), "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));
    REQUIRE(manager.CodeActionHintSpans(buffer).empty());
}

TEST_CASE("CodeActionHintSpans relocates a hint past an edit above it", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-code-action-hints-relocate-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("ab = 1;\ncd = 2;\n"); // line1 starts at byte 8
    buffer.SetDiagnostics({Buffer::Diagnostic{.startByte = 8, .endByte = 10, .severity = Buffer::Diagnostic::Severity::Error, .message = "bad"}});

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestCodeActionHints(buffer, 0, buffer.Size(), "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(raw)},
                               {"result", Json::array({{{"title", "fix it"},
                                                        {"kind", "quickfix"},
                                                        {"diagnostics", Json::array({{{"range", {{"start", {{"line", 1}, {"character", 0}}}, {"end", {{"line", 1}, {"character", 2}}}}}}})}}})}}
                              .dump());
    REQUIRE(manager.CodeActionHintSpans(buffer)[0].startByte == 8);

    buffer.SetPoint(0);
    buffer.InsertAtPoint("// note\n"); // 8 bytes above the flagged line

    const std::vector<Manager::CodeActionHint>& hints = manager.CodeActionHintSpans(buffer);
    REQUIRE(hints.size() == 1);
    REQUIRE(hints[0].startByte == 16); // 8 + 8, not left behind on the wrong line
}

TEST_CASE("A code-action error response stops this connection being asked again", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-code-action-hints-unsupported-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("ab = 1;\n");
    buffer.SetDiagnostics({Buffer::Diagnostic{.startByte = 0, .endByte = 2, .severity = Buffer::Diagnostic::Severity::Error, .message = "bad"}});

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestCodeActionHints(buffer, 0, buffer.Size(), "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(raw)},
                               {"error", {{"code", -32601}, {"message", "method not found"}}}}
                              .dump());

    // A different viewport, so the coverage gate isn't what's refusing.
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("ef = 3;\n");
    manager.RequestCodeActionHints(buffer, 0, buffer.Size(), "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

// region-scoped-relocation follow-up: an edit anywhere in the buffer used
// to blank every applied inlay hint (a global content-generation gate),
// which is exactly the "annotations vanish on every keystroke" complaint --
// InlayHintSpans now relocates a hint outside the edited region instead,
// the same CodeLensSpans-shaped lazy catch-up on read.
TEST_CASE("InlayHintSpans relocates hints outside the edited region instead of blanking the whole set", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-relocate-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("ab = 1;\ncd = 2;\n"); // line0 = 8 bytes [0,8), line1 starts at byte 8

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestInlayHints(buffer, 0, buffer.Size(), "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    const auto        response =
        Json{{"jsonrpc", "2.0"},
             {"id", RequestIdFromFrame(raw)},
             {"result", Json::array({{{"position", {{"line", 0}, {"character", 2}}}, {"label", ": int"}},     // after "ab", byte 2
                                     {{"position", {{"line", 1}, {"character", 2}}}, {"label", ": int"}}})}}; // after "cd", byte 10
    client->DispatchFrame(response.dump());
    REQUIRE(manager.InlayHintSpans(buffer).size() == 2);

    // Insert a whole new line between the two hints (at byte 8, the start of
    // line1) -- touches neither hint's own token. The line0 hint sits before
    // the insertion point and must not move; the line1 hint sits after it
    // and must shift by the inserted length, not vanish.
    buffer.SetPoint(8);
    buffer.InsertAtPoint("// note\n"); // 8 bytes inserted

    const std::vector<Manager::ResolvedInlayHint>& hints = manager.InlayHintSpans(buffer);
    REQUIRE(hints.size() == 2);
    REQUIRE(hints[0].byteOffset == 2);  // unmoved: strictly before the insertion point
    REQUIRE(hints[1].byteOffset == 18); // 10 + 8: shifted by the insert's own length
}

// buffer-anchored-lsp-results: the live phpantom_lsp symptom (2026-09-18).
// Typing at a hint's own anchor left the hint behind, so it rendered two
// columns early -- inside the identifier the keystrokes had just extended.
// The snapshot-diff path had no vocabulary for this: its one relocation rule
// left an offset sitting exactly on the change unmoved, which is right for a
// range's end and wrong for anything that precedes what it annotates.
TEST_CASE("A hint anchored at the insertion point follows the text it annotates", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-gravity-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("ab = 1;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    manager.RequestInlayHints(buffer, 0, buffer.Size(), "test-lang");
    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const auto        response = Json{
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"position", {{"line", 0}, {"character", 2}}}, {"label", ": int"}}})}, // byte 2, right after "ab"
    };
    client->DispatchFrame(response.dump());
    REQUIRE(manager.InlayHintSpans(buffer).size() == 1);

    // "ab" becomes "abcd" -- the hint annotates the identifier, so it has to
    // come with it.
    buffer.SetPoint(2);
    buffer.InsertAtPoint("cd");

    const std::vector<Manager::ResolvedInlayHint>& hints = manager.InlayHintSpans(buffer);
    REQUIRE(hints.size() == 1);
    REQUIRE(hints[0].byteOffset == 4);
}

// The other half of the same fix: a diff between two versions recovers one
// contiguous changed region, so two edits either side of a hint read as one
// span containing it and the whole set was dropped. Replaying the buffer's
// own edits has no such blind spot -- which is what a multi-cursor edit, a
// replace-all, or any burst of typing between two reads actually produces.
TEST_CASE("A hint between two separate edits survives both", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-two-edits-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("aa = 1;\nbb = 2;\ncc = 3;\n"); // lines start at 0, 8, 16

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    manager.RequestInlayHints(buffer, 0, buffer.Size(), "test-lang");
    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const auto        response = Json{
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"position", {{"line", 1}, {"character", 2}}}, {"label", ": int"}}})}, // byte 10, the middle line
    };
    client->DispatchFrame(response.dump());
    REQUIRE(manager.InlayHintSpans(buffer).size() == 1);

    // Two edits either side of the hint's own line, with no read in between.
    buffer.SetPoint(0);
    buffer.InsertAtPoint("x");
    buffer.SetPoint(24);
    buffer.InsertAtPoint("y");

    const std::vector<Manager::ResolvedInlayHint>& hints = manager.InlayHintSpans(buffer);
    REQUIRE(hints.size() == 1);
    REQUIRE(hints[0].byteOffset == 11); // shifted by the first edit only
}

// A whole-content swap has no edit to relocate through, so the set is
// dropped rather than left pointing into a document that no longer exists.
TEST_CASE("A wholesale content replacement drops the hint set", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-barrier-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("ab = 1;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    manager.RequestInlayHints(buffer, 0, buffer.Size(), "test-lang");
    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const auto        response = Json{
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"position", {{"line", 0}, {"character", 2}}}, {"label", ": int"}}})},
    };
    client->DispatchFrame(response.dump());
    REQUIRE(manager.InlayHintSpans(buffer).size() == 1);

    buffer.RestoreContent("something else entirely\n");
    REQUIRE(manager.InlayHintSpans(buffer).empty());
}

// The complementary case: a hint anchored INSIDE the edited region can't be
// trusted to relocate (its own token is what the edit rewrote), so it's
// dropped -- never clamped to the edit point, which for a hint (unlike a
// code lens) would render it inside whatever token now sits there.
TEST_CASE("InlayHintSpans drops a hint anchored inside the edited region instead of clamping it", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-drop-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("ab = 1;\ncd = 2;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    manager.RequestInlayHints(buffer, 0, buffer.Size(), "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    const auto        response =
        Json{{"jsonrpc", "2.0"},
             {"id", RequestIdFromFrame(raw)},
             {"result", Json::array({{{"position", {{"line", 0}, {"character", 2}}}, {"label", ": int"}},     // byte 2
                                     {{"position", {{"line", 1}, {"character", 2}}}, {"label", ": int"}}})}}; // byte 10
    client->DispatchFrame(response.dump());
    REQUIRE(manager.InlayHintSpans(buffer).size() == 2);

    // Delete "ab " (bytes [0,3)) -- strictly contains byte 2, the line0
    // hint's own anchor.
    buffer.SetPoint(0);
    buffer.DeleteRange(0, 3);

    const std::vector<Manager::ResolvedInlayHint>& hints = manager.InlayHintSpans(buffer);
    REQUIRE(hints.size() == 1);        // the line0 hint is gone, not garbled
    REQUIRE(hints[0].byteOffset == 7); // 10 - 3: the surviving hint still relocates correctly
}

// The receipt-path half of the same fix: a response can land after the
// buffer moved on from what was requested (SyncBuffer/RequestInlayHints are
// not re-invoked here, so the request-id latch alone would let this
// through) -- it must resolve against the document it was actually
// requested against, then relocate onto live content, not get discarded or
// misapplied against the wrong document.
TEST_CASE("A response landing after the buffer was edited relocates onto live content", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-race-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("ab = 1;\ncd = 2;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    manager.RequestInlayHints(buffer, 0, buffer.Size(), "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead); // request sent against the pre-edit document

    // The buffer moves on before the server answers.
    buffer.SetPoint(8);
    buffer.InsertAtPoint("// note\n");

    const auto response =
        Json{{"jsonrpc", "2.0"},
             {"id", RequestIdFromFrame(raw)},
             {"result", Json::array({{{"position", {{"line", 0}, {"character", 2}}}, {"label", ": int"}},     // byte 2 against the OLD document
                                     {{"position", {{"line", 1}, {"character", 2}}}, {"label", ": int"}}})}}; // byte 10 against the OLD document
    client->DispatchFrame(response.dump());

    const std::vector<Manager::ResolvedInlayHint>& hints = manager.InlayHintSpans(buffer);
    REQUIRE(hints.size() == 2);
    REQUIRE(hints[0].byteOffset == 2);  // before the insertion point, unmoved
    REQUIRE(hints[1].byteOffset == 18); // after it, shifted by the inserted length
}

// This used to assert the opposite of its third case: any viewport change at
// all was worth a fresh request, because the store only ever held the last
// response and a scroll really did have nothing left to show. With hints
// retained per answered range, re-asking about a range already answered buys
// nothing and costs a round trip per wheel notch -- so the contract is now
// "ask about ground nobody has asked about", and the margin is what makes an
// ordinary scroll land on covered ground.
TEST_CASE("RequestInlayHints asks only about ranges not already covered", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-dedup-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    for (int line = 0; line < 10; ++line) {
        buffer.InsertAtPoint("int x = 1;\n"); // 11 bytes a line, 110 total
    }

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    // One screenful visible; a screenful either side is what actually goes
    // out, so this request covers bytes 0-22.
    manager.RequestInlayHints(buffer, 0, 11, "test-lang");
    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const auto        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["params"]["range"]["start"]["line"] == 0);
    REQUIRE(request["params"]["range"]["end"]["line"] == 2); // the margin, not just what is visible

    manager.RequestInlayHints(buffer, 0, 11, "test-lang"); // a repaint, not a real change
    REQUIRE(NoFrameArrives(server.serverStdinRead));

    // The case that changed: scrolled a line, still inside what was asked
    // about. The old gate sent a second request here.
    manager.RequestInlayHints(buffer, 11, 22, "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));

    // Scrolled past the margin -- genuinely new ground, so one request.
    manager.RequestInlayHints(buffer, 44, 55, "test-lang");
    const std::string secondRaw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(secondRaw.substr(secondRaw.find("\r\n\r\n") + 4))["method"] == "textDocument/inlayHint");
}

TEST_CASE("An edit discards inlay hint coverage, since a hint anywhere may have changed", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-coverage-edit-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;\nint y = 2;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestInlayHints(buffer, 0, 11, "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"result", Json::array()}}.dump());

    manager.RequestInlayHints(buffer, 0, 11, "test-lang"); // answered already
    REQUIRE(NoFrameArrives(server.serverStdinRead));

    buffer.InsertAtPoint("x");
    manager.SyncBuffer(buffer, "test-lang");
    // RequestInlayHints will not ask about a generation the server has not
    // been sent yet, so the debounced didChange has to land first.
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    (void)ReadRawFrame(server.serverStdinRead);

    manager.RequestInlayHints(buffer, 0, 11, "test-lang");
    const std::string afterEdit = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(afterEdit.substr(afterEdit.find("\r\n\r\n") + 4))["method"] == "textDocument/inlayHint");
}

// The bug these three cover: a response used to replace the whole store, so
// scrolling to a new region deleted the hints for every region already
// answered. Visible as the code bouncing sideways for a round trip on every
// mouse-wheel notch, and as hints that never came back until you edited.
TEST_CASE("A response for one range leaves the hints already answered for another range alone", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-retain-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int a = 1;\nint b = 2;\nint c = 3;");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    // The first screenful: line 0.
    manager.RequestInlayHints(buffer, 0, 11, "test-lang");
    const std::string firstRaw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(firstRaw)},
                               {"result", Json::array({{{"position", {{"line", 0}, {"character", 3}}}, {"label", ": int"}}})}}
                              .dump());
    REQUIRE(manager.InlayHintSpans(buffer).size() == 1);

    // Scrolled to line 2, past the first request's margin. The server
    // answers only about what it was asked about, which is exactly how the
    // old code came to lose line 0.
    manager.RequestInlayHints(buffer, 22, buffer.Size(), "test-lang");
    const std::string secondRaw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(secondRaw)},
                               {"result", Json::array({{{"position", {{"line", 2}, {"character", 3}}}, {"label", ": int"}}})}}
                              .dump());

    const std::vector<Manager::ResolvedInlayHint>& hints = manager.InlayHintSpans(buffer);
    REQUIRE(hints.size() == 2);
    REQUIRE(hints[0].byteOffset == 3);  // line 0, kept across a scroll that never asked about it again
    REQUIRE(hints[1].byteOffset == 25); // line 2
}

TEST_CASE("A second response for the same range replaces that range's hints rather than duplicating them", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-replace-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int a = 1;\nint b = 2;\nint c = 3;");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestInlayHints(buffer, 0, 11, "test-lang");
    const std::string firstRaw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(firstRaw)},
                               {"result", Json::array({{{"position", {{"line", 0}, {"character", 3}}}, {"label", ": int"}}})}}
                              .dump());
    REQUIRE(manager.InlayHintSpans(buffer).size() == 1);

    // An edit is what makes already-answered ground askable again, so this
    // is also the only way back to the same range through the real path.
    buffer.InsertAtPoint(";"); // at end of buffer -- moves no earlier offset
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    (void)ReadRawFrame(server.serverStdinRead); // the debounced didChange

    // Answered again for the same ground, with a different label at the same
    // position: one hint, the newer label -- never two stacked.
    manager.RequestInlayHints(buffer, 0, 11, "test-lang");
    const std::string secondRaw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(secondRaw)},
                               {"result", Json::array({{{"position", {{"line", 0}, {"character", 3}}}, {"label", ": long"}}})}}
                              .dump());

    const std::vector<Manager::ResolvedInlayHint>& hints = manager.InlayHintSpans(buffer);
    REQUIRE(hints.size() == 1);
    REQUIRE(hints[0].label == ": long");
}

TEST_CASE("An empty response withdraws that range's hints and only that range's", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-withdraw-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int a = 1;\nint b = 2;\nint c = 3;");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestInlayHints(buffer, 0, 11, "test-lang");
    const std::string firstRaw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(firstRaw)},
                               {"result", Json::array({{{"position", {{"line", 0}, {"character", 3}}}, {"label", ": int"}}})}}
                              .dump());

    manager.RequestInlayHints(buffer, 22, buffer.Size(), "test-lang");
    const std::string secondRaw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(secondRaw)},
                               {"result", Json::array({{{"position", {{"line", 2}, {"character", 3}}}, {"label", ": int"}}})}}
                              .dump());
    REQUIRE(manager.InlayHintSpans(buffer).size() == 2);

    // An edit is what makes already-answered ground askable again, so this
    // is also the only way back to the same range through the real path.
    buffer.InsertAtPoint(";"); // at end of buffer -- moves no earlier offset
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    (void)ReadRawFrame(server.serverStdinRead); // the debounced didChange

    // Line 0's hints withdrawn. Retention must not mean "a hint can never go
    // away" -- an empty answer about a range is still an answer about it.
    manager.RequestInlayHints(buffer, 0, 11, "test-lang");
    const std::string thirdRaw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(thirdRaw)}, {"result", Json::array()}}.dump());

    const std::vector<Manager::ResolvedInlayHint>& hints = manager.InlayHintSpans(buffer);
    REQUIRE(hints.size() == 1);
    REQUIRE(hints[0].byteOffset == 25); // line 2 untouched
}

TEST_CASE("A server erroring on textDocument/inlayHint is never asked again for that connection's lifetime", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-unsupported-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("a");

    Client* client = nullptr;
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
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hints-disabled-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestInlayHints(buffer, 0, buffer.Size(), "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("NotifyBufferClosed sends didClose to every server the buffer was opened with", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-close-test.md";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("some text");

    Client* primaryClient = nullptr;
    Client* proseClient   = nullptr;
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
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-lsp-manager-prose-binary-test.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out.put('\0');
        out << "some content after a nul byte";
    }
    Buffer& buffer = bufferList.OpenOrCreateFile(path, /*allowBinary=*/true);

    Client* primaryClient = nullptr;
    Client* proseClient   = nullptr;
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
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-embedded-open-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    Client* htmlClient = nullptr;
    Client* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html");
    (void)ReadRawFrame(htmlServer.serverStdinRead); // drain html's own didOpen

    REQUIRE(NoFrameArrives(jsServer.serverStdinRead)); // not yet embedded-synced

    const std::string paddedText = "        let x = 1;          "; // stands in for real padding -- content unimportant here
    manager.SyncEmbeddedDocuments(
        buffer, {Manager::EmbeddedDocumentSync{.language = "javascript", .documentText = paddedText, .ownedRanges = {{8, 19}}}});

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
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ned-lsp-manager-embedded-incremental-test.html";
    Buffer& buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    Client* htmlClient = nullptr;
    Client* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);
    manager.SetTextDocumentSyncKindForTesting("javascript", TextDocumentSyncKind::Incremental);

    manager.SyncBuffer(buffer, "html");
    (void)ReadRawFrame(htmlServer.serverStdinRead); // drain html's own didOpen

    const std::string firstPadded = "        let x = 1;          ";
    manager.SyncEmbeddedDocuments(
        buffer, {Manager::EmbeddedDocumentSync{.language = "javascript", .documentText = firstPadded, .ownedRanges = {{8, 19}}}});
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
        {Manager::EmbeddedDocumentSync{.language = "javascript", .documentText = secondPadded, .ownedRanges = {{8, 20}}}});

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
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-embedded-teardown-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    Client* htmlClient = nullptr;
    Client* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html");
    (void)ReadRawFrame(htmlServer.serverStdinRead);
    manager.SyncEmbeddedDocuments(
        buffer, {Manager::EmbeddedDocumentSync{.language = "javascript", .documentText = "let x = 1;", .ownedRanges = {{0, 10}}}});
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
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-primary-ambiguity-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    Client* htmlClient  = nullptr;
    Client* proseClient = nullptr;
    Client* jsClient    = nullptr;
    FakeServer htmlServer  = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer proseServer = FakeServer::Create(manager, std::string(kProseLanguageKey), eventLoop, proseClient);
    FakeServer jsServer    = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html"); // primary ("html") + prose
    manager.SyncEmbeddedDocuments(
        buffer, {Manager::EmbeddedDocumentSync{.language = "javascript", .documentText = "let x = 1;", .ownedRanges = {{0, 10}}}});
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
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-embedded-diag-filter-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<div></div><script>let x = 1;</script>");

    Client* jsClient = nullptr;
    FakeServer jsServer = FakeServer::Create(manager, "javascript", eventLoop, jsClient);
    // Owned range covers only the "let x = 1;" content (offsets 20..30 in
    // the buffer above); everything else is padding as far as javascript is
    // concerned.
    manager.SyncEmbeddedDocuments(
        buffer, {Manager::EmbeddedDocumentSync{.language = "javascript", .documentText = buffer.Text(), .ownedRanges = {{20, 30}}}});
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

TEST_CASE("Manager::RequestHover with an explicit serverKey routes to that connection, not the primary one", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-hover-serverkey-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>let x = 1;</script>");

    Client* htmlClient = nullptr;
    Client* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html");
    manager.SyncEmbeddedDocuments(
        buffer, {Manager::EmbeddedDocumentSync{.language = "javascript", .documentText = "let x = 1;", .ownedRanges = {{0, 10}}}});
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

TEST_CASE("Manager::RequestDefinition with an explicit serverKey routes to that connection, not the primary one", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-definition-serverkey-test.html";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("<script>call_site();</script>");

    Client* htmlClient = nullptr;
    Client* jsClient   = nullptr;
    FakeServer htmlServer = FakeServer::Create(manager, "html", eventLoop, htmlClient);
    FakeServer jsServer   = FakeServer::Create(manager, "javascript", eventLoop, jsClient);

    manager.SyncBuffer(buffer, "html");
    manager.SyncEmbeddedDocuments(
        buffer, {Manager::EmbeddedDocumentSync{.language = "javascript", .documentText = "call_site();", .ownedRanges = {{0, 12}}}});
    (void)ReadRawFrame(htmlServer.serverStdinRead);
    (void)ReadRawFrame(jsServer.serverStdinRead);

    bool                                      invoked = false;
    std::vector<Manager::ResolvedLocation> got;
    manager.RequestDefinition(
        buffer, 0,
        [&](std::vector<Manager::ResolvedLocation> locations) {
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

// LSP-deliberate-cuts follow-up: BackgroundSyncEnabled is process-wide
// state (see BackgroundSync.h) -- every test that flips it must leave it
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
    Manager         manager(bufferList, eventLoop);

    const std::filesystem::path cPath    = std::filesystem::temp_directory_path() / "ned-lsp-background-sync-test.c";
    const std::filesystem::path pyPath   = std::filesystem::temp_directory_path() / "ned-lsp-background-sync-test.py";
    Buffer&                     cBuffer  = bufferList.OpenOrCreateFile(cPath);
    Buffer&                     pyBuffer = bufferList.OpenOrCreateFile(pyPath);

    Client* cClient  = nullptr;
    Client* pyClient = nullptr;
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
    Manager         manager(bufferList, eventLoop);

    Buffer& scratch = bufferList.CreateBuffer("scratch"); // no path
    (void)scratch;

    const std::filesystem::path loadingPath = std::filesystem::temp_directory_path() / "ned-lsp-background-sync-loading-test.c";
    Buffer&                     loading     = bufferList.OpenOrCreateFile(loadingPath);
    loading.MarkLoading();

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "c", eventLoop, client);

    ned::editor::lsp::SyncBackgroundBuffers(bufferList, manager); // must not crash and must not sync either buffer

    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("SyncBackgroundBuffers is a no-op entirely when disabled", "[Lsp]") {
    LspBackgroundSyncGuard guard;
    BufferList             bufferList;
    ned::ui::EventLoop     eventLoop;
    Manager             manager(bufferList, eventLoop);

    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-background-sync-disabled-test.c";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "c", eventLoop, client);

    ned::editor::lsp::SetLspBackgroundSyncEnabled(false);
    ned::editor::lsp::SyncBackgroundBuffers(bufferList, manager);

    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

// graceful-lsp-shutdown follow-up.
TEST_CASE("Manager::Shutdown sends shutdown then exit to a directly-spawned client", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client); // brokerBacked defaults to false

    manager.Shutdown();

    const std::vector<Json> frames = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 2));
    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0]["method"] == "shutdown");
    REQUIRE(frames[1]["method"] == "exit");
}

TEST_CASE("Manager::Shutdown never sends anything to a broker-backed client", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client, Json::object(), /*brokerBacked=*/true);

    manager.Shutdown();

    // A broker-owned server is shared with other ned processes and the
    // broker daemon itself -- it must keep running after this process
    // exits, so it must never receive this process's own shutdown/exit.
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

// documentLink follow-up.
TEST_CASE("Manager::RequestDocumentLinks resolves a file:// target to a path and byte offsets", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-document-link-test.c";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("#include \"own.h\"\nint main() {}\n");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    bool                                          invoked = false;
    std::vector<Manager::ResolvedDocumentLink> got;
    manager.RequestDocumentLinks(buffer, [&](std::vector<Manager::ResolvedDocumentLink> links) {
        invoked = true;
        got     = std::move(links);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/documentLink");
    REQUIRE(request["params"]["textDocument"].contains("uri"));
    REQUIRE_FALSE(request["params"].contains("position")); // whole-document scope, no position param

    const std::filesystem::path target   = std::filesystem::temp_directory_path() / "own.h";
    const Json                  response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"range",
                                  {{"start", {{"line", 0}, {"character", 9}}}, {"end", {{"line", 0}, {"character", 16}}}}},
                                 {"target", "file://" + target.string()}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].path == target);
    REQUIRE(got[0].url.empty());
    REQUIRE_FALSE(got[0].needsResolve);
    REQUIRE(got[0].startByte == 9); // "#include " is 9 bytes
    REQUIRE(got[0].endByte == 16);  // through the closing quote
}

TEST_CASE("Manager::RequestDocumentLinks keeps a non-file target as a url, not a path", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-document-link-url-test.c";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("// see https://example.com\n");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    std::vector<Manager::ResolvedDocumentLink> got;
    manager.RequestDocumentLinks(buffer, [&](std::vector<Manager::ResolvedDocumentLink> links) { got = std::move(links); });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"range",
                                  {{"start", {{"line", 0}, {"character", 7}}}, {"end", {{"line", 0}, {"character", 26}}}}},
                                 {"target", "https://example.com"}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(got.size() == 1);
    REQUIRE(got[0].path.empty());
    REQUIRE(got[0].url == "https://example.com");
}

TEST_CASE("Manager::RequestDocumentLinks marks a target-less link needsResolve and keeps its raw item", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-document-link-resolve-test.c";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("#include \"own.h\"\n");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    std::vector<Manager::ResolvedDocumentLink> got;
    manager.RequestDocumentLinks(buffer, [&](std::vector<Manager::ResolvedDocumentLink> links) { got = std::move(links); });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"range",
                                  {{"start", {{"line", 0}, {"character", 9}}}, {"end", {{"line", 0}, {"character", 16}}}}},
                                 {"data", {{"token", 42}}}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(got.size() == 1);
    REQUIRE(got[0].needsResolve);
    REQUIRE(got[0].path.empty());
    REQUIRE(got[0].url.empty());

    // The second hop: documentLink/resolve takes the original item back
    // verbatim as its whole params body, and the resolved target lands in
    // the same shape an inline one would have.
    const std::filesystem::path                     target = std::filesystem::temp_directory_path() / "own.h";
    std::optional<Manager::ResolvedDocumentLink> resolved;
    manager.ResolveDocumentLink(buffer, got[0],
                                [&](std::optional<Manager::ResolvedDocumentLink> link) { resolved = std::move(link); });

    const std::string resolveRaw     = ReadRawFrame(server.serverStdinRead);
    const Json        resolveRequest = Json::parse(resolveRaw.substr(resolveRaw.find("\r\n\r\n") + 4));
    REQUIRE(resolveRequest["method"] == "documentLink/resolve");
    REQUIRE(resolveRequest["params"]["data"]["token"] == 42);

    Json resolveResult         = resolveRequest["params"];
    resolveResult["target"]    = "file://" + target.string();
    const Json resolveResponse = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(resolveRaw)},
        {"result", resolveResult},
    };
    client->DispatchFrame(resolveResponse.dump());

    REQUIRE(resolved.has_value());
    REQUIRE_FALSE(resolved->needsResolve);
    REQUIRE(resolved->path == target);
    REQUIRE(resolved->startByte == 9); // the original range stands
    REQUIRE(resolved->endByte == 16);
}

TEST_CASE("Manager::RequestDocumentLinks stops asking a server that answered with an error", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-document-link-latch-test.c";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    bool firstInvoked = false;
    manager.RequestDocumentLinks(buffer, [&](std::vector<Manager::ResolvedDocumentLink>) { firstInvoked = true; });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"error", {{"code", -32601}, {"message", "method not found"}}},
    };
    client->DispatchFrame(response.dump());
    REQUIRE(firstInvoked);

    bool secondInvoked = false;
    manager.RequestDocumentLinks(buffer, [&](std::vector<Manager::ResolvedDocumentLink> links) {
        secondInvoked = true;
        REQUIRE(links.empty());
    });
    REQUIRE(secondInvoked);                          // answered synchronously off the latch
    REQUIRE(NoFrameArrives(server.serverStdinRead)); // and nothing went out on the wire
}

TEST_CASE("Manager::RequestDocumentLinks answers empty when the buffer was never synced", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.CreateBuffer("scratch");

    bool invoked = false;
    manager.RequestDocumentLinks(buffer, [&](std::vector<Manager::ResolvedDocumentLink> links) {
        invoked = true;
        REQUIRE(links.empty());
    });

    REQUIRE(invoked); // synchronous, which is what lets BufferView fall straight through to its own resolution
}

TEST_CASE("Manager percent-decodes a URI target before resolving it to a path", "[Lsp]") {
    // Found live against clangd, which reports a system include's target as
    // ".../g%2B%2B-v16/algorithm" -- undecoded, that path exists nowhere.
    // documentLink is just the cheapest request to assert it through; the
    // decoding sits in the shared uri->path boundary every location-shaped
    // response goes through.
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-uri-decode-test.c";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("#include <algorithm>\n");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    std::vector<Manager::ResolvedDocumentLink> got;
    manager.RequestDocumentLinks(buffer, [&](std::vector<Manager::ResolvedDocumentLink> links) { got = std::move(links); });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"range",
                                  {{"start", {{"line", 0}, {"character", 9}}}, {"end", {{"line", 0}, {"character", 20}}}}},
                                 {"target", "file:///usr/include/g%2B%2B-v16/a%20b/algorithm"}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(got.size() == 1);
    REQUIRE(got[0].path == std::filesystem::path("/usr/include/g++-v16/a b/algorithm"));
}

TEST_CASE("Manager leaves a stray percent sign in a URI alone", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                  manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-uri-stray-percent-test.c";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("#include \"x.h\"\n");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    std::vector<Manager::ResolvedDocumentLink> got;
    manager.RequestDocumentLinks(buffer, [&](std::vector<Manager::ResolvedDocumentLink> links) { got = std::move(links); });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", Json::array({{{"range",
                                  {{"start", {{"line", 0}, {"character", 9}}}, {"end", {{"line", 0}, {"character", 14}}}}},
                                 {"target", "file:///tmp/100%-done/x.h"}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(got.size() == 1);
    REQUIRE(got[0].path == std::filesystem::path("/tmp/100%-done/x.h"));
}

// ---------------------------------------------------------------------------
// completion-resolve / completion-trigger-characters
// ---------------------------------------------------------------------------

TEST_CASE("Manager::RequestCompletion reports triggerKind 2 and the character that caused it", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-completion-trigger-test.txt");
    buffer.InsertAtPoint("foo.");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    manager.RequestCompletion(buffer, buffer.Point(), [](ned::editor::lsp::CompletionList) {}, "test-lang", ".");

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["params"]["context"]["triggerKind"] == 2);
    REQUIRE(request["params"]["context"]["triggerCharacter"] == ".");
}

TEST_CASE("Manager::RequestCompletion folds allCommitCharacters into items that declared none", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-commit-chars-test.txt");
    buffer.InsertAtPoint("foo");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    // SetClientForTesting bypasses the handshake that would normally populate this.
    manager.SetCompletionProviderForTesting("test-lang", ned::editor::lsp::CompletionProviderInfo{.allCommitCharacters = {";"}});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    std::vector<CompletionItem> gotItems;
    manager.RequestCompletion(buffer, buffer.Point(), [&](ned::editor::lsp::CompletionList list) { gotItems = std::move(list.items); });

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result",
         {{"isIncomplete", false},
          {"items", Json::array({{{"label", "inherits"}}, {{"label", "keeps-own"}, {"commitCharacters", Json::array({"("})}}})}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(gotItems.size() == 2);
    CHECK(gotItems[0].commitCharacters == std::vector<std::string>{";"});
    CHECK(gotItems[1].commitCharacters == std::vector<std::string>{"("});
}

TEST_CASE("Manager::ResolveCompletionItem round-trips the item's own raw JSON", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-completion-resolve-test.txt");
    buffer.InsertAtPoint("vec");

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);

    CompletionItem item;
    item.label = "vector";
    item.raw   = Json{{"label", "vector"}, {"data", {{"symbolId", 42}}}};

    bool                          invoked = false;
    std::optional<CompletionItem> got;
    manager.ResolveCompletionItem(buffer, item, [&](std::optional<CompletionItem> resolved) {
        invoked = true;
        got     = std::move(resolved);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "completionItem/resolve");
    // The whole item goes back verbatim -- "data" is the server's own handle onto it.
    REQUIRE(request["params"]["data"]["symbolId"] == 42);

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result", {{"label", "vector"}, {"detail", "std::vector<T>"}, {"documentation", "A dynamic array."}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.has_value());
    CHECK(got->detail == "std::vector<T>");
    CHECK(got->documentation == "A dynamic array.");
}

TEST_CASE("Manager::ResolveCompletionItem answers nullopt for a synthesized item with no raw JSON", "[Lsp]") {
    // dabbrev/Janet-binding items are built by this editor, not parsed off
    // the wire -- there is nothing to hand back, and no request goes out.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);
    Buffer&            buffer = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "ned-lsp-resolve-synth-test.txt");

    CompletionItem synthesized;
    synthesized.label = "buffer-word";

    bool invoked = false;
    manager.ResolveCompletionItem(buffer, synthesized, [&](std::optional<CompletionItem> resolved) {
        invoked = true;
        CHECK_FALSE(resolved.has_value());
    });
    CHECK(invoked);
}

TEST_CASE("Manager captures completionProvider from a real initialize response", "[Lsp]") {
    // The handshake is what populates this in production; ClientDisconnected
    // must not leave a stale entry behind for a respawned server.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager         manager(bufferList, eventLoop);

    manager.SetCompletionProviderForTesting(
        "test-lang", ned::editor::lsp::CompletionProviderInfo{.triggerCharacters = {"."}, .resolveProvider = true});
    const auto provider = manager.CompletionProviderFor("test-lang");
    REQUIRE(provider.has_value());
    CHECK(provider->triggerCharacters == std::vector<std::string>{"."});
    CHECK(provider->resolveProvider);
    CHECK_FALSE(manager.CompletionProviderFor("other-lang").has_value());
}

TEST_CASE("BuildInitializeParams declares refreshSupport for every kind a server can ask to have re-pulled", "[Lsp]") {
    // Without these a well-behaved server never sends workspace/<kind>/refresh
    // at all, so the handlers that act on one would never fire. Object-shaped
    // per spec (the *WorkspaceClientCapabilities types), unlike the plain
    // booleans their siblings use -- and "diagnostics" is plural here while
    // the request it enables is the singular workspace/diagnostic/refresh.
    const Json  params    = ned::editor::lsp::BuildInitializeParams(std::filesystem::path("/some/project"));
    const Json& workspace = params.at("capabilities").at("workspace");
    for (const char* key : {"semanticTokens", "codeLens", "inlayHint", "diagnostics"}) {
        REQUIRE(workspace.at(key).at("refreshSupport") == true);
    }
}

TEST_CASE("workspace/inlayHint/refresh is answered with a null result and re-opens an already-answered viewport",
          "[Lsp]") {
    const RequestIdleGuard      idle(1);
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-hint-refresh-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;\nint y = 2;\nint z = 3;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting(
        "test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}, .rangeSupported = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestViewportFeatures(buffer, 0, 11, "test-lang");
    const std::vector<Json> first = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 3));
    REQUIRE(first.size() == 3);
    REQUIRE(first[1]["method"] == "textDocument/inlayHint");

    // An empty array is a real answer, not a decline -- which is exactly the
    // shape a server uses when it cannot serve hints yet, and it leaves the
    // viewport marked covered.
    client->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"id", first[1]["id"]}, {"result", Json::array()}}.dump());
    manager.RequestViewportFeatures(buffer, 0, 11, "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));

    client->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"id", 4242}, {"method", "workspace/inlayHint/refresh"}}.dump());
    manager.RequestViewportFeatures(buffer, 0, 11, "test-lang");

    const std::vector<Json> afterRefresh = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 2));
    REQUIRE(afterRefresh.size() == 2);
    REQUIRE(afterRefresh[0]["id"] == 4242);
    REQUIRE(afterRefresh[0]["result"].is_null());
    REQUIRE(!afterRefresh[0].contains("error")); // an unhandled method would have answered MethodNotFound
    REQUIRE(afterRefresh[1]["method"] == "textDocument/inlayHint");
}

// A lens with no command is a lens the client is expected to resolve before
// showing -- and jdtls answers every one of them that way, so a client that
// only displays what arrives inline shows nothing at all under Java. The
// resolve goes out on its own, and its answer fills in the TITLE only: the
// stored entry's offsets have been carried forward while the request was in
// flight, and the reply's own copies are the stale ones it was sent with.
TEST_CASE("A lens with no command is resolved, and the answer moves no offsets", "[Lsp]") {
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-codelens-resolve-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int alpha = 1;\nint beta = 2;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestCodeLenses(buffer, 0, buffer.Content().ByteLength(), "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["method"] == "textDocument/codeLens");

    const Json unresolved{{"range", {{"start", {{"line", 1}, {"character", 0}}}, {"end", {{"line", 1}, {"character", 3}}}}},
                          {"data", Json::array({"references"})}};
    client->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", RequestIdFromFrame(raw)}, {"result", Json::array({unresolved})}}.dump());

    // Nothing to show yet -- but the lens is kept, and the resolve for it
    // goes out carrying the server's own object back verbatim.
    REQUIRE(manager.CodeLensSpans(buffer).size() == 1);
    REQUIRE(manager.CodeLensSpans(buffer)[0].title.empty());
    const std::string resolveRaw   = ReadRawFrame(server.serverStdinRead);
    const Json        resolveFrame = Json::parse(resolveRaw.substr(resolveRaw.find("\r\n\r\n") + 4));
    REQUIRE(resolveFrame["method"] == "codeLens/resolve");
    REQUIRE(resolveFrame["params"] == unresolved);

    // Asking again for the same set must not ask the server twice.
    manager.ResolveViewportCodeLenses(buffer, 0, buffer.Content().ByteLength(), "test-lang");
    REQUIRE(NoFrameArrives(server.serverStdinRead));

    // The buffer moves on while the resolve is still in flight.
    buffer.SetPoint(0);
    buffer.InsertAtPoint("// header\n");

    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(resolveRaw)},
                               {"result", Json{{"range",
                                                {{"start", {{"line", 1}, {"character", 0}}},
                                                 {"end", {{"line", 1}, {"character", 3}}}}},
                                               {"command", {{"title", "2 references"}, {"command", "noop"}}}}}}
                              .dump());

    REQUIRE(manager.CodeLensSpans(buffer).size() == 1);
    REQUIRE(manager.CodeLensSpans(buffer)[0].title == "2 references");
    REQUIRE(manager.CodeLensSpans(buffer)[0].hasCommand);
    // Line 1 of the document the resolve spoke about is line 2 here.
    REQUIRE(buffer.Content().ByteOffsetToLine(manager.CodeLensSpans(buffer)[0].startByte) == 2);
}

TEST_CASE("workspace/codeLens/refresh re-asks for a document whose lenses were already fetched", "[Lsp]") {
    const RequestIdleGuard      idle(1);
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-code-lens-refresh-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestCodeLenses(buffer, 0, buffer.Content().ByteLength(), "test-lang");
    const std::vector<Json> first = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 1));
    REQUIRE(first.size() == 1);
    REQUIRE(first[0]["method"] == "textDocument/codeLens");
    client->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"id", first[0]["id"]}, {"result", Json::array()}}.dump());

    manager.RequestCodeLenses(buffer, 0, buffer.Content().ByteLength(), "test-lang"); // same content generation -- already asked
    REQUIRE(NoFrameArrives(server.serverStdinRead));

    client->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"id", 7}, {"method", "workspace/codeLens/refresh"}}.dump());
    manager.RequestCodeLenses(buffer, 0, buffer.Content().ByteLength(), "test-lang");
    const std::vector<Json> afterRefresh = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 2));
    REQUIRE(afterRefresh.size() == 2);
    REQUIRE(afterRefresh[0]["result"].is_null());
    REQUIRE(afterRefresh[1]["method"] == "textDocument/codeLens");
}

TEST_CASE("workspace/diagnostic/refresh pulls diagnostics again with no edit behind it", "[Lsp]") {
    const PullDiagnosticsEnabledGuard guard;
    BufferList                        bufferList;
    ned::ui::EventLoop                eventLoop;
    Manager                           manager(bufferList, eventLoop);
    const std::filesystem::path       path = std::filesystem::temp_directory_path() / "ned-lsp-manager-diagnostic-refresh-test.txt";
    Buffer&                           buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad code");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");

    const std::vector<Json> first = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 2));
    REQUIRE(first.size() == 2);
    REQUIRE(first[1]["method"] == "textDocument/diagnostic");
    client->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", first[1]["id"]}, {"result", {{"kind", "full"}, {"items", Json::array()}}}}.dump());

    // Nothing re-pulls diagnostics on its own cadence -- they ride
    // didOpen/didChange -- so this is the whole of what refresh has to do.
    client->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"id", 11}, {"method", "workspace/diagnostic/refresh"}}.dump());
    // The re-request goes out ahead of the refresh's own response: the
    // handler runs to produce that response, and sending is what it does.
    const std::vector<Json> afterRefresh = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 2));
    REQUIRE(afterRefresh.size() == 2);
    REQUIRE(afterRefresh[0]["method"] == "textDocument/diagnostic");
    REQUIRE(afterRefresh[1]["id"] == 11);
    REQUIRE(afterRefresh[1]["result"].is_null());
}

TEST_CASE("A viewport request that settles unanswered is asked again instead of being held back by the armed triple",
          "[Lsp]") {
    const RequestIdleGuard      idle(1);
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-viewport-decline-retry-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;\nint y = 2;\nint z = 3;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting(
        "test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}, .rangeSupported = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestViewportFeatures(buffer, 0, 11, "test-lang");
    const std::vector<Json> first = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 3));
    REQUIRE(first.size() == 3);
    REQUIRE(first[0]["method"] == "textDocument/semanticTokens/range");

    // A real error latches this server as not honoring the range request and
    // wants the whole-document request to follow -- which the armed
    // (generation, viewport) triple used to suppress entirely, since neither
    // the content nor the viewport had moved.
    client->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", first[0]["id"]}, {"error", {{"code", -32601}, {"message", "no range support"}}}}.dump());

    // Inside the throttle window this time (the first send is milliseconds
    // old), so the retry rides the deferred fire rather than a leading edge.
    manager.RequestViewportFeatures(buffer, 0, 11, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::vector<Json> retried = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 1));
    REQUIRE(retried.size() == 1);
    REQUIRE(retried[0]["method"] == "textDocument/semanticTokens/full");
}

TEST_CASE("A ContentModified error leaves the hints already on screen alone and does not latch inlay hints off",
          "[Lsp]") {
    // The live shape this exists for: a server whose own index lags the
    // buffer declines a request it would otherwise answer wrongly. Answering
    // null instead would be read as "no hints here" and wipe the range,
    // which is the blink -- and answering any other error code would
    // disable hints for the rest of the connection.
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-inlay-content-modified-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("f(1);\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestInlayHints(buffer, 0, 6, "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(raw)},
                               {"result", Json::array({{{"position", {{"line", 0}, {"character", 2}}}, {"label", "n:"}}})}}
                              .dump());
    REQUIRE(manager.InlayHintSpans(buffer).size() == 1);

    buffer.InsertAtPoint("g();\n");
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    (void)ReadRawFrame(server.serverStdinRead); // drain didChange

    manager.RequestInlayHints(buffer, 0, 11, "test-lang");
    const std::string declined = ReadRawFrame(server.serverStdinRead);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", RequestIdFromFrame(declined)},
                               {"error", {{"code", -32801}, {"message", "content modified"}}}}
                              .dump());

    // Still there, carried onto the edited content rather than replaced by
    // an answer the server never actually gave.
    REQUIRE(manager.InlayHintSpans(buffer).size() == 1);

    // And not latched: a further viewport still asks.
    manager.RequestInlayHints(buffer, 0, 11, "test-lang");
    const std::vector<Json> retried = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 1));
    REQUIRE(retried.size() == 1);
    REQUIRE(retried[0]["method"] == "textDocument/inlayHint");
}

TEST_CASE("A ContentModified error on semanticTokens/range keeps the range path rather than falling back to full",
          "[Lsp]") {
    const RequestIdleGuard      idle(1);
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-semantic-content-modified-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;\nint y = 2;\nint z = 3;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting(
        "test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}, .rangeSupported = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    manager.RequestViewportFeatures(buffer, 0, 11, "test-lang");
    const std::vector<Json> first = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 3));
    REQUIRE(first.size() == 3);
    REQUIRE(first[0]["method"] == "textDocument/semanticTokens/range");

    // -32601 here would latch rangeUnsupported_ and send full next; -32801
    // says only that this one request was about a document that moved.
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", first[0]["id"]},
                               {"error", {{"code", -32801}, {"message", "content modified"}}}}
                              .dump());

    manager.RequestViewportFeatures(buffer, 0, 11, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::vector<Json> retried = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 1));
    REQUIRE(retried.size() == 1);
    REQUIRE(retried[0]["method"] == "textDocument/semanticTokens/range");
}

TEST_CASE("A server declining every request with ContentModified is retried once per triple, not forever", "[Lsp]") {
    // Nothing latches a retryable code off, so the retry budget is the only
    // thing standing between a permanently-declining server and a request
    // per throttle window for as long as the buffer stays open.
    const RequestIdleGuard      idle(1);
    BufferList                  bufferList;
    ned::ui::EventLoop          eventLoop;
    Manager                     manager(bufferList, eventLoop);
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-content-modified-budget-test.txt";
    Buffer&                     buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("int x = 1;\nint y = 2;\nint z = 3;\n");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetSemanticTokensLegendForTesting(
        "test-lang", SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}, .rangeSupported = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    const auto declineEverything = [&](const std::vector<Json>& frames) {
        for (const Json& frame : frames) {
            client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                                       {"id", frame["id"]},
                                       {"error", {{"code", -32801}, {"message", "content modified"}}}}
                                      .dump());
        }
    };

    manager.RequestViewportFeatures(buffer, 0, 11, "test-lang");
    const std::vector<Json> first = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 3));
    REQUIRE(first.size() == 3);
    declineEverything(first);

    manager.RequestViewportFeatures(buffer, 0, 11, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    // Two, not three: codeLens stamped this content generation as already
    // requested when it was first sent, and an error does not unstamp it.
    const std::vector<Json> retried = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 2));
    REQUIRE(retried.size() == 2); // the one retry this triple is owed
    declineEverything(retried);

    // Budget spent: the same triple asks nothing more, however many frames
    // paint over it.
    for (int frame = 0; frame < 3; ++frame) {
        manager.RequestViewportFeatures(buffer, 0, 11, "test-lang");
        eventLoop.DrainPosted_();
    }
    REQUIRE(NoFrameArrives(server.serverStdinRead));

    // A refresh is the server itself saying the answer changed, and buys a
    // fresh retry.
    client->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"id", 99}, {"method", "workspace/inlayHint/refresh"}}.dump());
    manager.RequestViewportFeatures(buffer, 0, 11, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    const std::vector<Json> afterRefresh = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 3));
    REQUIRE(afterRefresh.size() == 3); // the refresh's own response, then the two re-asked requests
    const bool askedAgain = std::any_of(afterRefresh.begin(), afterRefresh.end(), [](const Json& frame) {
        return frame.contains("method") && frame["method"] == "textDocument/inlayHint";
    });
    REQUIRE(askedAgain);
}

// dynamic-registration follow-up: a server declaring a capability after the
// handshake. Before this, client/registerCapability fell through to
// DispatchFrame's generic MethodNotFound and the capability was lost with
// nothing to explain it -- see Manager::HandleRegisterCapability.
TEST_CASE("Manager answers client/registerCapability and records what it registered", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    const Json request = {{"jsonrpc", "2.0"},
                          {"id", 40},
                          {"method", "client/registerCapability"},
                          {"params",
                           {{"registrations",
                             Json::array({{{"id", "watch-1"},
                                           {"method", "workspace/didChangeWatchedFiles"},
                                           {"registerOptions", {{"watchers", Json::array({{{"globPattern", "**/*.php"}}})}}}}})}}}};
    client->DispatchFrame(request.dump());

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(response["id"] == 40);
    REQUIRE(response["result"].is_null()); // void per spec -- an error response is what the server used to get
    REQUIRE_FALSE(response.contains("error"));

    REQUIRE(manager.DynamicRegistrationsFor("test-lang").size() == 1);
    CHECK(manager.DynamicRegistrationsFor("test-lang")[0].method == "workspace/didChangeWatchedFiles");
    CHECK(manager.HasWatchedFileRegistrations());
}

TEST_CASE("Manager drops a registration client/unregisterCapability names", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", 41},
                               {"method", "client/registerCapability"},
                               {"params",
                                {{"registrations", Json::array({{{"id", "watch-1"},
                                                                 {"method", "workspace/didChangeWatchedFiles"},
                                                                 {"registerOptions",
                                                                  {{"watchers", Json::array({{{"globPattern", "**/*.php"}}})}}}}})}}}}
                              .dump());
    (void)ReadRawFrame(server.serverStdinRead); // drain the registration's own response
    REQUIRE(manager.HasWatchedFileRegistrations());

    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", 42},
                               {"method", "client/unregisterCapability"},
                               {"params", {{"unregisterations", Json::array({{{"id", "watch-1"}, {"method", "workspace/didChangeWatchedFiles"}}})}}}}
                              .dump());
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["result"].is_null());
    CHECK(manager.DynamicRegistrationsFor("test-lang").empty());
    CHECK_FALSE(manager.HasWatchedFileRegistrations());
}

// lsp-did-change-watched-files follow-up: the other half of a registration.
TEST_CASE("Manager sends didChangeWatchedFiles only for paths a registered watcher matched", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    client->DispatchFrame(
        Json{{"jsonrpc", "2.0"},
             {"id", 43},
             {"method", "client/registerCapability"},
             {"params",
              {{"registrations",
                Json::array({{{"id", "watch-1"},
                              {"method", "workspace/didChangeWatchedFiles"},
                              // Brace alternation is what servers actually register with, and
                              // fnmatch has none of its own -- see MatchesWatcherGlob.
                              {"registerOptions", {{"watchers", Json::array({{{"globPattern", "**/*.{php,inc}"}}})}}}}})}}}}
            .dump());
    (void)ReadRawFrame(server.serverStdinRead); // drain the registration's own response

    manager.NotifyWatchedFilesChanged({
        {.path = "/project/src/Thing.php", .type = Manager::FileChangeType::Changed},
        {.path = "/project/src/legacy.inc", .type = Manager::FileChangeType::Created},
        {.path = "/project/src/other.go", .type = Manager::FileChangeType::Changed}, // no watcher wants this
    });

    const std::string raw   = ReadRawFrame(server.serverStdinRead);
    const Json        frame = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(frame["method"] == "workspace/didChangeWatchedFiles");
    const Json& changes = frame["params"]["changes"];
    REQUIRE(changes.size() == 2);
    CHECK(changes[0]["uri"] == "file:///project/src/Thing.php");
    CHECK(changes[0]["type"] == 2); // FileChangeType::Changed, LSP's own numbering
    CHECK(changes[1]["uri"] == "file:///project/src/legacy.inc");
    CHECK(changes[1]["type"] == 1); // Created
}

TEST_CASE("Manager honors a watcher's kind mask and stays silent with no registration", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    // No registration yet: a server that declares its capabilities
    // statically must not be sent a notification it never asked for.
    manager.NotifyWatchedFilesChanged({{.path = "/project/a.php", .type = Manager::FileChangeType::Changed}});
    REQUIRE(NoFrameArrives(server.serverStdinRead));

    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", 44},
                               {"method", "client/registerCapability"},
                               {"params",
                                {{"registrations",
                                  Json::array({{{"id", "watch-1"},
                                                {"method", "workspace/didChangeWatchedFiles"},
                                                {"registerOptions",
                                                 // kind 1 == Created only.
                                                 {{"watchers", Json::array({{{"globPattern", "**/*.php"}, {"kind", 1}}})}}}}})}}}}
                              .dump());
    (void)ReadRawFrame(server.serverStdinRead);

    manager.NotifyWatchedFilesChanged({{.path = "/project/a.php", .type = Manager::FileChangeType::Changed}});
    REQUIRE(NoFrameArrives(server.serverStdinRead)); // the watcher asked about creation, not modification

    manager.NotifyWatchedFilesChanged({{.path = "/project/a.php", .type = Manager::FileChangeType::Created}});
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["params"]["changes"].size() == 1);
}

// did-save follow-up: a server that re-runs analysis on save (which is how
// most linter integrations behave) never learned a save happened.
TEST_CASE("SyncBuffer sends didSave after a save when the server declared save support", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);
    const auto         path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-didsave-test.txt";
    Buffer&            buffer = bufferList.OpenOrCreateFile(path);

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetTextDocumentSaveSupportForTesting("test-lang", ned::editor::lsp::TextDocumentSaveSupport{.supported = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    // A save with no edit behind it bumps no content generation, so this is
    // the path SyncBuffer would otherwise treat as a pure no-op.
    buffer.Save();
    manager.SyncBuffer(buffer, "test-lang");

    const std::string raw   = ReadRawFrame(server.serverStdinRead);
    const Json        frame = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(frame["method"] == "textDocument/didSave");
    CHECK(frame["params"]["textDocument"]["uri"].get<std::string>().ends_with("ned-lsp-manager-didsave-test.txt"));
    CHECK_FALSE(frame["params"].contains("text")); // includeText wasn't asked for

    // Reported once, not on every subsequent sync.
    manager.SyncBuffer(buffer, "test-lang");
    CHECK(NoFrameArrives(server.serverStdinRead));
    std::filesystem::remove(path);
}

TEST_CASE("didSave carries the document text when the server asked for includeText", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);
    const auto         path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-didsave-includetext-test.txt";
    Buffer&            buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("saved contents");

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetTextDocumentSaveSupportForTesting(
        "test-lang", ned::editor::lsp::TextDocumentSaveSupport{.supported = true, .includeText = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    buffer.Save();
    manager.SyncBuffer(buffer, "test-lang");
    const std::string raw   = ReadRawFrame(server.serverStdinRead);
    const Json        frame = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(frame["method"] == "textDocument/didSave");
    // The document as this client holds it, which is what the spec asks for
    // -- the save's own ensure-final-newline pass shapes the file on disk,
    // not the buffer.
    CHECK(frame["params"]["text"] == "saved contents");
    std::filesystem::remove(path);
}

TEST_CASE("No didSave reaches a server that never advertised save support", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);
    const auto         path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-didsave-unwanted-test.txt";
    Buffer&            buffer = bufferList.OpenOrCreateFile(path);

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    buffer.Save();
    manager.SyncBuffer(buffer, "test-lang");
    CHECK(NoFrameArrives(server.serverStdinRead));
    std::filesystem::remove(path);
}

TEST_CASE("A dynamic textDocument/didSave registration is honored like the static capability", "[Lsp]") {
    // A server declaring capabilities dynamically sends no textDocumentSync
    // at all, so the registration is the only thing that ever says "tell me
    // about saves".
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);
    const auto         path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-didsave-dynamic-test.txt";
    Buffer&            buffer = bufferList.OpenOrCreateFile(path);

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", 45},
                               {"method", "client/registerCapability"},
                               {"params", {{"registrations", Json::array({{{"id", "save-1"}, {"method", "textDocument/didSave"}}})}}}}
                              .dump());
    (void)ReadRawFrame(server.serverStdinRead); // drain the registration's own response

    buffer.Save();
    manager.SyncBuffer(buffer, "test-lang");
    const std::string raw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["method"] == "textDocument/didSave");
    std::filesystem::remove(path);
}

TEST_CASE("didSave follows the didChange carrying the content that was saved", "[Lsp]") {
    // Order is the point: a server told about a save before the content it
    // saved would re-analyze the previous text.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);
    const auto         path   = std::filesystem::temp_directory_path() / "ned-lsp-manager-didsave-order-test.txt";
    Buffer&            buffer = bufferList.OpenOrCreateFile(path);

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SetTextDocumentSaveSupportForTesting("test-lang", ned::editor::lsp::TextDocumentSaveSupport{.supported = true});
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    buffer.InsertAtPoint("edited");
    buffer.Save();
    manager.SyncBuffer(buffer, "test-lang"); // content changed too, so this one goes through the debounce
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });

    const std::vector<Json> frames = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 2));
    REQUIRE(frames.size() == 2);
    CHECK(frames[0]["method"] == "textDocument/didChange");
    CHECK(frames[1]["method"] == "textDocument/didSave");
    std::filesystem::remove(path);
}

// window-messages follow-up: three server->client surfaces that were dropped
// outright, so a server reporting a problem through the protocol (rather
// than through stderr) said nothing to the user at all.
TEST_CASE("Manager routes window/showMessage to the status handler and the lsp log", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);

    std::string status;
    manager.SetStatusMessageHandler([&status](std::string message) { status = std::move(message); });

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"method", "window/showMessage"},
                               {"params", {{"type", 2}, {"message", "index is out of date"}}}}
                              .dump());

    CHECK(status == "test-lang: index is out of date");
    const Buffer* log = bufferList.Find(std::string(ned::editor::lsp::kLspLogBufferName));
    REQUIRE(log != nullptr);
    CHECK(log->Text().find("index is out of date") != std::string::npos);
}

TEST_CASE("Manager answers window/showMessageRequest with null and names the actions offered", "[Lsp]") {
    // Client::RequestHandler is synchronous, so a modal choice driven from
    // here isn't possible -- ned shows what was asked and answers "none",
    // which is exactly what a dismissed message means.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);

    std::string status;
    manager.SetStatusMessageHandler([&status](std::string message) { status = std::move(message); });

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", 46},
                               {"method", "window/showMessageRequest"},
                               {"params",
                                {{"type", 3},
                                 {"message", "Reload the workspace?"},
                                 {"actions", Json::array({{{"title", "Reload"}}, {{"title", "Later"}}})}}}}
                              .dump());

    const std::string raw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["result"].is_null());
    CHECK(status == "test-lang: Reload the workspace? [Reload | Later]");
}

TEST_CASE("Manager routes window/showDocument's file uri through the handler", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);

    std::optional<Manager::ShowDocumentRequest> shown;
    manager.SetShowDocumentHandler([&shown](const Manager::ShowDocumentRequest& request) {
        shown = request;
        return true;
    });

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                               {"id", 47},
                               {"method", "window/showDocument"},
                               {"params",
                                {{"uri", "file:///project/src/main.rs"},
                                 {"takeFocus", false},
                                 {"selection",
                                  {{"start", {{"line", 11}, {"character", 4}}}, {"end", {{"line", 11}, {"character", 9}}}}}}}}
                              .dump());

    const std::string raw = ReadRawFrame(server.serverStdinRead);
    REQUIRE(Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["result"]["success"] == true);
    REQUIRE(shown.has_value());
    CHECK(shown->path == std::filesystem::path("/project/src/main.rs"));
    CHECK_FALSE(shown->takeFocus);
    REQUIRE(shown->position.has_value());
    CHECK(shown->position->line == 11);
}

TEST_CASE("Manager reports window/showDocument as unsuccessful with no handler wired up", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    client->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", 48}, {"method", "window/showDocument"}, {"params", {{"uri", "file:///a.c"}}}}.dump());

    const std::string raw      = ReadRawFrame(server.serverStdinRead);
    const Json        response = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    CHECK(response["result"]["success"] == false); // never an error response, per spec
    CHECK_FALSE(response.contains("error"));
}

TEST_CASE("BuildInitializeParams declares didSave and dynamic file-watcher registration", "[Lsp]") {
    // Both are what make the *server* act: a server sends its own
    // textDocumentSync.save back only to a client that says it wants
    // didSave, and registers file watchers only against a client that
    // accepts dynamic registrations at all.
    const Json params = ned::editor::lsp::BuildInitializeParams("/tmp/ned-capability-declaration-test");
    CHECK(params["capabilities"]["textDocument"]["synchronization"]["didSave"] == true);
    CHECK(params["capabilities"]["workspace"]["didChangeWatchedFiles"]["dynamicRegistration"] == true);
    // willSaveWaitUntil stays undeclared on purpose -- ned runs its own
    // format-on-save pipeline rather than the spec's.
    CHECK_FALSE(params["capabilities"]["textDocument"]["synchronization"].contains("willSaveWaitUntil"));
}

// file-operation-create-delete follow-up: the create and delete halves of
// the family whose rename half already shipped.
TEST_CASE("Manager::RequestWillDeleteFiles sends bare uris and resolves the server's WorkspaceEdit", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);
    manager.SetFileOperationFiltersForTesting("test-lang", {.willDeleteGlobs = {"**/*.ts"}});

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    const std::filesystem::path doomed = std::filesystem::temp_directory_path() / "ned-will-delete.ts";

    bool                                   invoked = false;
    std::optional<Manager::ResolvedRename> got;
    manager.RequestWillDeleteFiles({doomed}, [&](std::optional<Manager::ResolvedRename> result) {
        invoked = true;
        got     = std::move(result);
    });

    const std::string raw     = ReadRawFrame(server.serverStdinRead);
    const Json        request = Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "workspace/willDeleteFiles");
    REQUIRE(request["params"]["files"].size() == 1);
    // A FileDelete carries one uri, where a FileRename carries oldUri/newUri.
    const std::string uri = request["params"]["files"][0]["uri"].get<std::string>();
    REQUIRE(uri.ends_with("ned-will-delete.ts"));
    REQUIRE_FALSE(request["params"]["files"][0].contains("oldUri"));

    const Json response = {
        {"jsonrpc", "2.0"},
        {"id", RequestIdFromFrame(raw)},
        {"result",
         {{"changes",
           {{"file:///tmp/importer.ts",
             Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 1}, {"character", 0}}}}},
                           {"newText", ""}}})}}}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(invoked);
    REQUIRE(got.has_value());
    REQUIRE(got->hasEdit);
    REQUIRE(got->edits.size() == 1);
    CHECK(got->edits[0].path == std::filesystem::path("/tmp/importer.ts"));
}

TEST_CASE("Manager::RequestWillDeleteFiles resolves inline when no server declared a willDelete filter", "[Lsp]") {
    // The property BufferView::PerformProjectDelete relies on to stay
    // synchronous in every build with no LSP server in play.
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);
    manager.SetFileOperationFiltersForTesting("test-lang", {.didRenameGlobs = {"**/*.ts"}}); // rename only

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);

    bool                                   invoked = false;
    std::optional<Manager::ResolvedRename> got;
    manager.RequestWillDeleteFiles({"/tmp/ned-unwatched.ts"}, [&](std::optional<Manager::ResolvedRename> result) {
        invoked = true;
        got     = std::move(result);
    });

    REQUIRE(invoked); // synchronously, before any round trip
    CHECK_FALSE(got.has_value());
    CHECK(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("Manager::NotifyFilesDeleted and NotifyFilesCreated match their own filters, not each other's", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);
    manager.SetFileOperationFiltersForTesting("create-lang", {.didCreateGlobs = {"**/*.ts"}});
    manager.SetFileOperationFiltersForTesting("delete-lang", {.didDeleteGlobs = {"**/*.ts"}});

    Client*    createClient = nullptr;
    Client*    deleteClient = nullptr;
    FakeServer createServer = FakeServer::Create(manager, "create-lang", eventLoop, createClient);
    FakeServer deleteServer = FakeServer::Create(manager, "delete-lang", eventLoop, deleteClient);

    manager.NotifyFilesCreated({"/tmp/ned-created.ts"});
    const std::string createdRaw   = ReadRawFrame(createServer.serverStdinRead);
    const Json        createdFrame = Json::parse(createdRaw.substr(createdRaw.find("\r\n\r\n") + 4));
    CHECK(createdFrame["method"] == "workspace/didCreateFiles");
    CHECK(createdFrame["params"]["files"][0]["uri"] == "file:///tmp/ned-created.ts");
    CHECK_FALSE(createdFrame.contains("id")); // a notification, not a request
    CHECK(NoFrameArrives(deleteServer.serverStdinRead));

    manager.NotifyFilesDeleted({"/tmp/ned-deleted.ts"});
    const std::string deletedRaw   = ReadRawFrame(deleteServer.serverStdinRead);
    const Json        deletedFrame = Json::parse(deletedRaw.substr(deletedRaw.find("\r\n\r\n") + 4));
    CHECK(deletedFrame["method"] == "workspace/didDeleteFiles");
    CHECK(deletedFrame["params"]["files"][0]["uri"] == "file:///tmp/ned-deleted.ts");
    CHECK(NoFrameArrives(createServer.serverStdinRead));
}

TEST_CASE("SyncBuffer reports a file the first save brought into existence", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);
    manager.SetFileOperationFiltersForTesting("test-lang", {.didCreateGlobs = {"**/*.txt"}});

    const auto path = std::filesystem::temp_directory_path() / "ned-lsp-manager-didcreate-test.txt";
    std::filesystem::remove(path);
    Buffer& buffer = bufferList.OpenOrCreateFile(path); // no file on disk yet -- C-x C-f on a new name

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead);      // drain didOpen
    REQUIRE(NoFrameArrives(server.serverStdinRead)); // nothing created yet -- the buffer is all there is

    buffer.InsertAtPoint("first contents");
    buffer.Save();
    manager.SyncBuffer(buffer, "test-lang");

    const std::vector<Json> frames = ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 1));
    REQUIRE_FALSE(frames.empty());
    CHECK(frames[0]["method"] == "workspace/didCreateFiles");
    CHECK(frames[0]["params"]["files"][0]["uri"].get<std::string>().ends_with("ned-lsp-manager-didcreate-test.txt"));

    // Reported once: a second save of a file that now exists is not a creation.
    buffer.InsertAtPoint("more");
    buffer.Save();
    manager.SyncBuffer(buffer, "test-lang");
    WaitUntil(eventLoop, [&] { return !NoFrameArrives(server.serverStdinRead); });
    for (const Json& frame : ParseAllFrames(ReadRawFramesUntil(server.serverStdinRead, 1))) {
        CHECK(frame["method"] != "workspace/didCreateFiles");
    }
    std::filesystem::remove(path);
}

TEST_CASE("SyncBuffer reports no creation for a file that was already on disk", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);
    manager.SetFileOperationFiltersForTesting("test-lang", {.didCreateGlobs = {"**/*.txt"}});

    const auto path = std::filesystem::temp_directory_path() / "ned-lsp-manager-didcreate-existing-test.txt";
    std::ofstream(path) << "already here\n";
    Buffer& buffer = bufferList.OpenOrCreateFile(path);

    Client*    client = nullptr;
    FakeServer server = FakeServer::Create(manager, "test-lang", eventLoop, client);
    manager.SyncBuffer(buffer, "test-lang");
    (void)ReadRawFrame(server.serverStdinRead); // drain didOpen

    buffer.Save();
    manager.SyncBuffer(buffer, "test-lang");
    CHECK(NoFrameArrives(server.serverStdinRead));
    std::filesystem::remove(path);
}

TEST_CASE("BuildInitializeParams advertises the create/delete file operations but not willCreate", "[Lsp]") {
    const Json  params  = ned::editor::lsp::BuildInitializeParams("/tmp/ned-file-operations-test");
    const Json& fileOps = params["capabilities"]["workspace"]["fileOperations"];
    CHECK(fileOps["didCreate"] == true);
    CHECK(fileOps["willDelete"] == true);
    CHECK(fileOps["didDelete"] == true);
    // ned has no create-a-file action to request edits before, so claiming
    // willCreate would invite a request it can never send.
    CHECK_FALSE(fileOps.contains("willCreate"));
}

// project-wide-diagnostics follow-up. A server that checks the whole project
// reports most of its findings about files nobody has opened -- measured
// against rust-analyzer (cargo check) and gopls (per package), both of which
// publish a compile error in a file the editor never opened. Those used to be
// dropped, because a diagnostic's only home was a resident text::Buffer.
namespace {

// The publish notification a server sends about one file, with one
// diagnostic per message given.
Json ProjectPublish(const std::filesystem::path& path, const std::vector<std::string>& messages) {
    Json items = Json::array();
    for (const std::string& message : messages) {
        items.push_back({{"range", {{"start", {{"line", 1}, {"character", 2}}}, {"end", {{"line", 1}, {"character", 6}}}}},
                         {"severity", 1},
                         {"message", message}});
    }
    return Json{{"jsonrpc", "2.0"},
                {"method", "textDocument/publishDiagnostics"},
                {"params", {{"uri", "file://" + path.string()}, {"diagnostics", items}}}};
}

} // namespace

TEST_CASE("Diagnostics for a file with no open buffer are kept and reported", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ned-project-diagnostics-test";
    std::filesystem::create_directories(root);
    const std::filesystem::path unopened = root / "never-opened.rs";
    std::ofstream(unopened) << "fn value() -> i32 {\n    \"nope\"\n}\n";

    Client* client = nullptr;
    FakeServer server = FakeServer::Create(manager, "proj-lang", eventLoop, client);
    manager.SetConnectionFoldersForTesting("proj-lang", {root});

    client->DispatchFrame(ProjectPublish(unopened, {"mismatched types"}).dump());
    WaitUntil(eventLoop, [&] { return !manager.ProjectDiagnostics().empty(); });

    const auto files = manager.ProjectDiagnostics();
    REQUIRE(files.size() == 1);
    REQUIRE(files[0].path == unopened);
    REQUIRE(files[0].diagnostics.size() == 1);
    REQUIRE(files[0].diagnostics[0].message == "mismatched types");
    // Positions stay in the server's own space: there is no resident content
    // to resolve them against.
    REQUIRE(files[0].diagnostics[0].start.line == 1);
    REQUIRE(files[0].diagnostics[0].start.character == 2);

    SECTION("an empty publish is how a server says fixed, and drops the file") {
        client->DispatchFrame(ProjectPublish(unopened, {}).dump());
        WaitUntil(eventLoop, [&] { return manager.ProjectDiagnostics().empty(); });
        REQUIRE(manager.ProjectDiagnostics().empty());
    }

    SECTION("a resident buffer owns its own diagnostics, so the record steps aside") {
        // Deliberately not driven by didOpen/didClose: the read is gated on
        // whether a buffer exists at the moment the answer is used, so the
        // same store can never double-report alongside Buffer::Diagnostics().
        Buffer& buffer = bufferList.OpenOrCreateFile(unopened);
        const std::string name = buffer.Name();
        REQUIRE(manager.ProjectDiagnostics().empty());

        REQUIRE(bufferList.Close(name));
        REQUIRE(manager.ProjectDiagnostics().size() == 1); // and comes back when it closes
    }

    SECTION("a file outside every folder the connection serves is not this project's problem") {
        const std::filesystem::path outside = std::filesystem::temp_directory_path() / "ned-project-diagnostics-outsider.rs";
        client->DispatchFrame(ProjectPublish(outside, {"not ours"}).dump());
        // Waits for it to show up on the same terms as the accepted one
        // above; the assertion is that it never does.
        WaitUntil(eventLoop, [&] { return manager.ProjectDiagnostics().size() > 1; });
        const auto after = manager.ProjectDiagnostics();
        REQUIRE(after.size() == 1);
        REQUIRE(after[0].path == unopened);
    }

    std::filesystem::remove_all(root);
}

// A dead server's findings are not facts about the project any more, and
// these entries have no buffer to reach them through -- so unlike
// diagnosticsBySource_, which ClientDisconnected clears via each affected
// buffer's own sync state, these are keyed by connection and named directly.
TEST_CASE("A disconnect drops that connection's project diagnostics", "[Lsp]") {
    BufferList         bufferList;
    ned::ui::EventLoop eventLoop;
    Manager            manager(bufferList, eventLoop);

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ned-project-diagnostics-disconnect";
    std::filesystem::create_directories(root);
    const std::filesystem::path unopened = root / "gone.rs";

    Client* client = nullptr;
    auto    server = std::make_optional<FakeServer>(FakeServer::Create(manager, "drop-lang", eventLoop, client));
    manager.SetConnectionFoldersForTesting("drop-lang", {root});

    client->DispatchFrame(ProjectPublish(unopened, {"mismatched types"}).dump());
    WaitUntil(eventLoop, [&] { return !manager.ProjectDiagnostics().empty(); });
    REQUIRE(manager.ProjectDiagnostics().size() == 1);

    server.reset(); // EOF on the fake server's write end -- the real disconnect path
    WaitUntil(eventLoop, [&] { return manager.ProjectDiagnostics().empty(); });
    REQUIRE(manager.ProjectDiagnostics().empty());

    std::filesystem::remove_all(root);
}
