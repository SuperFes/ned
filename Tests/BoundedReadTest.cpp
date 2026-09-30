#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include <unistd.h>

#include "BoundedRead.h"

using ned::test::TryBoundedRead;

namespace {

struct Pipe {
    int read  = -1;
    int write = -1;

    Pipe() {
        int fds[2];
        REQUIRE(::pipe(fds) == 0);
        read  = fds[0];
        write = fds[1];
    }
    ~Pipe() {
        CloseWrite();
        ::close(read);
    }
    Pipe(const Pipe&)            = delete;
    Pipe& operator=(const Pipe&) = delete;

    void CloseWrite() {
        if (write >= 0) {
            ::close(write);
            write = -1;
        }
    }
};

} // namespace

TEST_CASE("TryBoundedRead returns what is already written", "[BoundedRead]") {
    Pipe pipe;
    REQUIRE(::write(pipe.write, "abc", 3) == 3);

    char buffer[8];
    REQUIRE(TryBoundedRead(pipe.read, buffer, sizeof(buffer), std::chrono::milliseconds(1000)) == 3);
    REQUIRE(std::string(buffer, 3) == "abc");
}

TEST_CASE("TryBoundedRead gives up when nothing arrives in time", "[BoundedRead]") {
    Pipe pipe;
    char buffer[8];

    const auto start = std::chrono::steady_clock::now();
    REQUIRE_FALSE(TryBoundedRead(pipe.read, buffer, sizeof(buffer), std::chrono::milliseconds(50)).has_value());
    REQUIRE(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(50));
}

TEST_CASE("TryBoundedRead reports a closed peer as end of file", "[BoundedRead]") {
    Pipe pipe;
    pipe.CloseWrite();

    char buffer[8];
    REQUIRE(TryBoundedRead(pipe.read, buffer, sizeof(buffer), std::chrono::milliseconds(1000)) == 0);
}
