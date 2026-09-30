//
// ::read for a test waiting on a peer. A bare ::read blocks the whole run
// when the message it waits for never comes; this fails the test instead.
//

#ifndef NED_TESTS_BOUNDEDREAD_H
#define NED_TESTS_BOUNDEDREAD_H

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <optional>

#include <poll.h>
#include <unistd.h>

namespace ned::test {

inline constexpr std::chrono::milliseconds kBoundedReadTimeout{30'000};

// ::read's result once fd is readable, or nullopt if it isn't within timeout.
inline std::optional<ssize_t> TryBoundedRead(int fd, void* buffer, std::size_t length, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            return std::nullopt;
        }
        pollfd    pfd{.fd = fd, .events = POLLIN, .revents = 0};
        const int ready = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
        if (ready == 0 || (ready < 0 && errno == EINTR)) {
            continue;
        }
        return ::read(fd, buffer, length);
    }
}

// TryBoundedRead that fails the test on timeout. Call it from the test's own
// thread only.
inline ssize_t BoundedRead(int fd, void* buffer, std::size_t length, std::chrono::milliseconds timeout = kBoundedReadTimeout) {
    const std::optional<ssize_t> n = TryBoundedRead(fd, buffer, length, timeout);
    if (!n) {
        FAIL("nothing readable on fd " << fd << " within " << timeout.count() << " ms");
    }
    return *n;
}

} // namespace ned::test

#endif // NED_TESTS_BOUNDEDREAD_H
