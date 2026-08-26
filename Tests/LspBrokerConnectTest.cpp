#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

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
