//
// A child on a pseudo-terminal: its stdio is a tty of the size asked for,
// in the directory and environment asked for, and it hears about resizes.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "Editor/Process/ChildProcess.h"
#include "Editor/Process/Pty.h"

using Catch::Matchers::ContainsSubstring;
using ned::editor::process::ChildProcess;
using ned::editor::process::SpawnOnPty;
using ned::editor::process::SpawnOptions;
using ned::editor::process::TerminalSize;

namespace {

// A sandbox without /dev/ptmx can't run any of this.
bool PtysAvailable() {
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master < 0) {
        return false;
    }
    ::close(master);
    return true;
}

// The output so far once it contains needle, or all of it at EOF or after
// a few seconds.
std::string ReadUntil(const ChildProcess& child, std::string& output, const std::string& needle) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (output.find(needle) == std::string::npos && std::chrono::steady_clock::now() < deadline) {
        const std::optional<std::string> chunk = child.ReadSome(std::chrono::milliseconds(100));
        if (chunk && chunk->empty()) {
            break;
        }
        if (chunk) {
            output += *chunk;
        }
    }
    return output;
}

std::string ReadToEnd(const ChildProcess& child) {
    std::string output;
    return ReadUntil(child, output, std::string(1, '\0'));
}

} // namespace

TEST_CASE("A child on a pty has a terminal of the size asked for", "[Process][Pty]") {
    if (!PtysAvailable()) {
        SKIP("no ptys here");
    }
    ChildProcess      child  = SpawnOnPty({"sh", "-c", "tty; stty size"}, TerminalSize{.rows = 30, .cols = 100}, SpawnOptions{});
    const std::string output = ReadToEnd(child);
    CHECK_THAT(output, ContainsSubstring("/dev/pts/"));
    CHECK_THAT(output, ContainsSubstring("30 100"));
    CHECK(child.WaitForExit() == 0);
}

TEST_CASE("A child on a pty hears that its terminal was resized", "[Process][Pty]") {
    if (!PtysAvailable()) {
        SKIP("no ptys here");
    }
    ChildProcess child = SpawnOnPty({"sh", "-c", "stty -echo; echo ready; read line; stty size"}, TerminalSize{.rows = 24, .cols = 80}, SpawnOptions{});
    std::string  output;
    REQUIRE_THAT(ReadUntil(child, output, "ready"), ContainsSubstring("ready"));
    child.ResizeTerminal(TerminalSize{.rows = 40, .cols = 120});
    child.WriteAll("\n", std::chrono::seconds(5));
    CHECK_THAT(ReadUntil(child, output, "40 120"), ContainsSubstring("40 120"));
}

TEST_CASE("A child on a pty starts in the directory and environment asked for", "[Process][Pty]") {
    if (!PtysAvailable()) {
        SKIP("no ptys here");
    }
    const std::filesystem::path dir = std::filesystem::canonical(std::filesystem::temp_directory_path());
    ::setenv("NED_PTY_TEST_GONE", "here", 1);
    ChildProcess child = SpawnOnPty({"sh", "-c", "printf '%s a=%s b=%s.\\n' \"$PWD\" \"$NED_PTY_TEST\" \"$NED_PTY_TEST_GONE\""}, TerminalSize{},
                                    SpawnOptions{.cwd = dir, .env = {{"NED_PTY_TEST", "yes"}, {"NED_PTY_TEST_GONE", std::nullopt}}});
    ::unsetenv("NED_PTY_TEST_GONE");
    CHECK_THAT(ReadToEnd(child), ContainsSubstring(dir.string() + " a=yes b=."));
}

TEST_CASE("A pty spawn that can't start throws", "[Process][Pty]") {
    CHECK_THROWS_AS(SpawnOnPty({}, TerminalSize{}, SpawnOptions{}), std::runtime_error);
    CHECK_THROWS_AS(SpawnOnPty({"ned-definitely-not-a-real-binary-xyz"}, TerminalSize{}, SpawnOptions{}), std::runtime_error);
    CHECK_THROWS_AS(SpawnOnPty({"true"}, TerminalSize{}, SpawnOptions{.cwd = "/ned/definitely/not/a/directory"}), std::runtime_error);
}

TEST_CASE("A child spawned later holds no pty master open", "[Process][Pty]") {
    if (!PtysAvailable()) {
        SKIP("no ptys here");
    }
    ChildProcess shell = SpawnOnPty({"sleep", "30"}, TerminalSize{}, SpawnOptions{});
    ChildProcess later({"sh", "-c", "ls -l /proc/self/fd/"});
    std::string  output;
    for (std::string chunk; !(chunk = later.ReadSome()).empty();) {
        output += chunk;
    }
    CHECK_FALSE(output.empty());
    CHECK_THAT(output, !ContainsSubstring("ptmx"));
}

TEST_CASE("Resizing a child that isn't on a pty does nothing", "[Process][Pty]") {
    ChildProcess child({"true"});
    child.ResizeTerminal(TerminalSize{.rows = 10, .cols = 10});
    CHECK(child.WaitForExit() == 0);
}
