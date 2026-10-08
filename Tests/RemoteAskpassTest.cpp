#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "Editor/Remote/Askpass.h"

using ned::editor::remote::AskpassKind;
using ned::editor::remote::AskpassListener;
using ned::editor::remote::AskpassRequest;
using ned::editor::remote::ClassifyAskpass;
using ned::editor::remote::RunAskpassRelay;

namespace {

// A fresh, short directory: sockets must fit sun_path.
std::filesystem::path FreshDir() {
    std::string pattern = (std::filesystem::temp_directory_path() / "ned-askpass-XXXXXX").string();
    REQUIRE(::mkdtemp(pattern.data()) != nullptr);
    return pattern;
}

struct Relayed {
    int         exitCode = -1;
    std::string printed;
};

// Runs the relay against listener, answering its question with answer.
Relayed Relay(AskpassListener& listener, const std::string& prompt, const std::string& hint, const ned::editor::remote::AskpassAnswer& answer,
              std::optional<AskpassRequest>* seen = nullptr) {
    int pipeFds[2];
    REQUIRE(::pipe(pipeFds) == 0);
    Relayed relayed;
    {
        std::jthread relay([&] { relayed.exitCode = RunAskpassRelay(listener.Path(), prompt, hint, pipeFds[1]); });
        pollfd       readable{.fd = listener.Fd(), .events = POLLIN, .revents = 0};
        REQUIRE(::poll(&readable, 1, 5000) == 1);
        std::optional<AskpassRequest> request = listener.Accept(std::chrono::seconds(5));
        REQUIRE(request);
        CHECK(request->Prompt() == prompt);
        CHECK(request->Hint() == hint);
        request->Reply(answer);
        if (seen != nullptr) {
            *seen = std::move(request);
        }
    }
    ::close(pipeFds[1]);
    char buffer[256];
    for (ssize_t length = 0; (length = ::read(pipeFds[0], buffer, sizeof buffer)) > 0;) {
        relayed.printed.append(buffer, static_cast<std::size_t>(length));
    }
    ::close(pipeFds[0]);
    return relayed;
}

} // namespace

TEST_CASE("ssh's questions are classified by its hint and the host-key wording", "[Remote][Askpass]") {
    REQUIRE(ClassifyAskpass("", "user@host's password: ") == AskpassKind::Secret);
    REQUIRE(ClassifyAskpass("", "Enter passphrase for key '/home/u/.ssh/id_ed25519': ") == AskpassKind::Secret);
    REQUIRE(ClassifyAskpass("", "The authenticity of host 'h' can't be established.\nAre you sure you want to continue connecting (yes/no/[fingerprint])? ") ==
            AskpassKind::Text);
    REQUIRE(ClassifyAskpass("confirm", "Allow use of key id_ed25519?") == AskpassKind::Confirm);
    REQUIRE(ClassifyAskpass("none", "Confirm user presence for key ED25519-SK") == AskpassKind::Notice);
}

TEST_CASE("The relay prints the editor's answer and exits 0", "[Remote][Askpass]") {
    const std::filesystem::path dir = FreshDir();
    {
        AskpassListener listener(dir / "a");
        const Relayed   relayed = Relay(listener, "fake@host's password: ", "", std::string("hunter2"));
        REQUIRE(relayed.exitCode == 0);
        REQUIRE(relayed.printed == "hunter2\n");

        // A prompt can run over several lines; an answer can be empty.
        const Relayed multiline = Relay(listener, "line one\nline two? ", "confirm", std::string());
        REQUIRE(multiline.exitCode == 0);
        REQUIRE(multiline.printed == "\n");
    }
    REQUIRE_FALSE(std::filesystem::exists(dir / "a"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("A cancelled question exits 1 having printed nothing", "[Remote][Askpass]") {
    const std::filesystem::path dir = FreshDir();
    AskpassListener             listener(dir / "a");
    const Relayed               relayed = Relay(listener, "Password: ", "", std::nullopt);
    REQUIRE(relayed.exitCode == 1);
    REQUIRE(relayed.printed.empty());

    // So does one with no editor listening.
    REQUIRE(RunAskpassRelay(dir / "nobody", "Password: ", "", STDOUT_FILENO) == 1);
    std::filesystem::remove_all(dir);
}

TEST_CASE("A relay that goes away is noticed while its question is open", "[Remote][Askpass]") {
    const std::filesystem::path   dir = FreshDir();
    AskpassListener               listener(dir / "a");
    std::optional<AskpassRequest> request;
    {
        // The relay itself waits for an answer, so the connection that goes
        // away here is the relay's end of a question that was never asked.
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        REQUIRE(fd >= 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        listener.Path().string().copy(address.sun_path, sizeof address.sun_path - 1);
        REQUIRE(::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0);
        const std::string question("\0Password: \0", 12);
        REQUIRE(::write(fd, question.data(), question.size()) == static_cast<ssize_t>(question.size()));
        request = listener.Accept(std::chrono::seconds(5));
        REQUIRE(request);
        pollfd open{.fd = request->Fd(), .events = POLLIN, .revents = 0};
        REQUIRE(::poll(&open, 1, 0) == 0);
        ::close(fd);
    }
    pollfd gone{.fd = request->Fd(), .events = POLLIN, .revents = 0};
    REQUIRE(::poll(&gone, 1, 5000) == 1);
    std::filesystem::remove_all(dir);
}
