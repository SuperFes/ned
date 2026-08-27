#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "Editor/Lsp/LspBrokerConnect.h"
#include "Editor/Lsp/Transport.h"
#include "UI/EventLoop.h"

using ned::editor::lsp::Json;
using ned::editor::lsp::Transport;
using ned::editor::lsp::TryConnectToBroker;

namespace {

    // A unique path per test run under /tmp -- short enough to stay well
    // under sockaddr_un's sun_path limit even on a deep CI tmpdir, unlike
    // this codebase's usual scratchpad convention.
    std::filesystem::path UniqueSocketPath() {
        return std::filesystem::path("/tmp") / ("ned-broker-connect-test-" + std::to_string(::getpid()) + ".sock");
    }

    // Same "real Notcurses context, scoped tightly to one TEST_CASE, never
    // shared process-wide" reasoning LspClientTest.cpp's own ClientFixture
    // documents -- see that file's header comment for why.
    struct EventLoopFixture {
        ned::ui::EventLoop eventLoop;
    };

} // namespace

TEST_CASE("TryConnectToBroker returns nullptr when nothing is listening", "[LspBrokerConnect]") {
    EventLoopFixture fixture;
    const auto        result = TryConnectToBroker("/some/project", "cpp", {"clangd"}, fixture.eventLoop, UniqueSocketPath());
    REQUIRE(result == nullptr);
}

TEST_CASE("TryConnectToBroker attaches over a real socket and sends a well-formed attach frame", "[LspBrokerConnect]") {
    const std::filesystem::path socketPath = UniqueSocketPath();
    ::unlink(socketPath.c_str());

    const int listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    REQUIRE(listenFd >= 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);
    REQUIRE(::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    REQUIRE(::listen(listenFd, 1) == 0);

    std::atomic<bool> receivedGoodFrame{false};
    std::thread       fakeBroker([listenFd, &receivedGoodFrame] {
        const int clientFd = ::accept(listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            return;
        }
        const int      dupFd = ::dup(clientFd);
        Transport      transport(clientFd, dupFd, -1);
        const auto     frameText = transport.ReadFrame();
        if (!frameText) {
            return;
        }
        const Json frame = Json::parse(*frameText);
        if (frame.value("method", std::string()) != "ned/broker-attach") {
            return;
        }
        const Json params = frame.value("params", Json::object());
        if (params.value("projectRoot", std::string()) != "/some/project") {
            return;
        }
        if (params.value("language", std::string()) != "cpp") {
            return;
        }
        if (!params.contains("argv") || params.at("argv") != Json::array({"clangd", "--foo"})) {
            return;
        }
        receivedGoodFrame = true;
    });

    EventLoopFixture fixture;
    auto              result = TryConnectToBroker("/some/project", "cpp", {"clangd", "--foo"}, fixture.eventLoop, socketPath);
    REQUIRE(result != nullptr);

    fakeBroker.join();
    REQUIRE(receivedGoodFrame.load());

    result.reset(); // before the fixture's own EventLoop/fake listener go out of scope
    ::close(listenFd);
    ::unlink(socketPath.c_str());
}

TEST_CASE("TryConnectToBroker gives up within its own timeout instead of hanging when the listener never accepts",
          "[LspBrokerConnect]") {
    // editor-side-connect-timeout follow-up: confirmed live -- a plain
    // blocking ::connect() froze a real, interactive `ned` process's main
    // UI thread solid (unix_wait_for_peer, unrecoverable short of kill -9)
    // once the real broker daemon's listen() backlog filled up and its
    // accept loop stalled. Reproduces the same "backlog full, nobody ever
    // accept()s" condition with a backlog of exactly 1 and one filler
    // connection occupying it -- TryConnectToBroker must return nullptr
    // within roughly its own timeout, not hang for the test's lifetime.
    const std::filesystem::path socketPath = UniqueSocketPath();
    ::unlink(socketPath.c_str());

    const int listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    REQUIRE(listenFd >= 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);
    REQUIRE(::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    REQUIRE(::listen(listenFd, 1) == 0);

    // A single filler connection wasn't enough to reliably reproduce a
    // blocking connect() on this kernel (some slack beyond the nominal
    // backlog is tolerated) -- enough filler connections to comfortably
    // exceed any such slack, none ever accepted. Non-blocking, deliberately:
    // a *plain blocking* filler connect() is exactly the bug under test, so
    // filling the backlog that way would hang this setup loop itself the
    // same way it hung the real editor -- confirmed live while writing this
    // test.
    std::vector<int> fillerFds;
    for (int i = 0; i < 32; ++i) {
        const int fillerFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        REQUIRE(fillerFd >= 0);
        const int flags = ::fcntl(fillerFd, F_GETFL, 0);
        ::fcntl(fillerFd, F_SETFL, flags | O_NONBLOCK);
        ::connect(fillerFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)); // expected to start failing/queuing once the backlog is exceeded
        fillerFds.push_back(fillerFd);
    }

    EventLoopFixture fixture;
    const auto       start   = std::chrono::steady_clock::now();
    const auto       result  = TryConnectToBroker("/some/project", "cpp", {"clangd"}, fixture.eventLoop, socketPath);
    const auto       elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE(result == nullptr);
    REQUIRE(elapsed < std::chrono::seconds(1)); // bounded, not hung for the test's own lifetime

    for (const int fillerFd : fillerFds) {
        ::close(fillerFd);
    }
    ::close(listenFd);
    ::unlink(socketPath.c_str());
}
