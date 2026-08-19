#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Editor/Process/ChildProcess.h"

using ned::editor::process::ChildProcess;
using ned::editor::process::StderrMode;

TEST_CASE("ChildProcess constructor throws for an executable that can't be found on $PATH", "[Process]") {
    REQUIRE_THROWS_AS(ChildProcess({"ned-definitely-not-a-real-binary-xyz"}), std::runtime_error);
}

TEST_CASE("ChildProcess constructor throws for an empty argv", "[Process]") {
    REQUIRE_THROWS_AS(ChildProcess(std::vector<std::string>{}), std::runtime_error);
}

TEST_CASE("ChildProcess round-trips data through a real pipe pair with no subprocess", "[Process]") {
    int toChild[2];
    int toParent[2];
    REQUIRE(::pipe(toChild) == 0);
    REQUIRE(::pipe(toParent) == 0);

    // parent-facing wrapper: writes to toChild[1], reads from toParent[0]
    ChildProcess parent(toParent[0], toChild[1]);
    // the "child side," driven directly by the test: reads toChild[0], writes toParent[1]
    ChildProcess childSide(toChild[0], toParent[1]);

    parent.WriteAll("hello");
    REQUIRE(childSide.ReadSome() == "hello");

    childSide.WriteAll("world");
    REQUIRE(parent.ReadSome() == "world");
}

TEST_CASE("ChildProcess::ReadSome returns an empty string on a clean EOF", "[Process]") {
    int toChild[2];
    REQUIRE(::pipe(toChild) == 0);
    ChildProcess reader(toChild[0], -1);
    {
        ChildProcess writer(-1, toChild[1]);
        writer.WriteAll("only chunk");
        REQUIRE(reader.ReadSome() == "only chunk");
        // writer goes out of scope here -- its destructor closes the write
        // end, which is what should make the next ReadSome see a clean EOF.
    }

    REQUIRE(reader.ReadSome().empty());
}

TEST_CASE("ChildProcess spawns a real process and exchanges data with it over pipes", "[Process]") {
    // Same stdbuf -o0 /bin/cat trick LspTransportTest.cpp already relies on
    // -- cat's stdout is fully buffered against a pipe, so without this the
    // test would hang waiting for output that's sitting in a buffer.
    ChildProcess child({"stdbuf", "-o0", "/bin/cat"});

    child.WriteAll("hello from a test");
    REQUIRE(child.ReadSome() == "hello from a test");
}

TEST_CASE("ChildProcess::Kill terminates a real running process promptly", "[Process]") {
    ChildProcess child({"sleep", "100"});
    const pid_t  pid = child.Pid();
    REQUIRE(pid > 0);

    child.Kill();

    // The process must actually be gone -- kill(pid, 0) fails with ESRCH
    // once it's been reaped and no other process has reused the pid.
    REQUIRE(::kill(pid, 0) != 0);
}

TEST_CASE("ChildProcess::WaitForExit reports a real exit code", "[Process]") {
    ChildProcess child({"sh", "-c", "exit 7"});

    // No output expected -- ReadSome blocks until EOF, which happens once
    // the shell exits and closes its (merged, in this default Discard mode,
    // stdout-only) fd.
    REQUIRE(child.ReadSome().empty());

    const auto exitCode = child.WaitForExit();
    REQUIRE(exitCode.has_value());
    REQUIRE(*exitCode == 7);
}

TEST_CASE("ChildProcess::WaitForExit reports nullopt after Kill", "[Process]") {
    ChildProcess child({"sleep", "100"});
    child.Kill();

    // Kill() already reaps and clears the managed pid, so WaitForExit finds
    // nothing left to wait on -- matches the documented "safe to call even
    // if Kill() was called first" contract.
    REQUIRE_FALSE(child.WaitForExit().has_value());
}

TEST_CASE("ChildProcess merges stderr onto stdout when requested", "[Process]") {
    ChildProcess child({"sh", "-c", "echo out; echo err >&2"}, StderrMode::MergeWithStdout);

    std::string collected;
    while (true) {
        const std::string chunk = child.ReadSome();
        if (chunk.empty()) {
            break;
        }
        collected += chunk;
    }

    // Shell buffering makes strict interleaving order non-deterministic --
    // assert both lines arrived, not their relative order.
    REQUIRE(collected.find("out") != std::string::npos);
    REQUIRE(collected.find("err") != std::string::npos);
}
