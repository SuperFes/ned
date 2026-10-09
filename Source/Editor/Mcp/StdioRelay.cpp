#include "StdioRelay.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ned::editor::mcp {

int RunStdioRelay(const std::string& socketPathStr, std::string_view label) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << label << ": socket() failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socketPathStr.size() >= sizeof(addr.sun_path)) {
        std::cerr << label << ": socket path too long: " << socketPathStr << '\n';
        ::close(fd);
        return 1;
    }
    std::strncpy(addr.sun_path, socketPathStr.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << label << ": connect() failed: " << std::strerror(errno) << '\n';
        ::close(fd);
        return 1;
    }

    // Two threads, one per direction -- a single thread can't select()
    // between "data on stdin" and "data on the socket" without extra
    // plumbing, and this process does nothing else, so two blocking pump
    // loops are simpler than one poll() loop for no real cost.
    std::jthread stdinToSocket([fd] {
        char buffer[4096];

        while (true) {
            const ssize_t n = ::read(STDIN_FILENO, buffer, sizeof(buffer));

            if (n <= 0) {
                break;
            }

            ssize_t written = 0;

            while (written < n) {
                const ssize_t result = ::write(fd, buffer + written, static_cast<std::size_t>(n - written));

                if (result <= 0) {
                    return;
                }

                written += result;
            }
        }

        ::shutdown(fd, SHUT_WR);
    });

    char buffer[4096];
    while (true) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        ssize_t written = 0;
        while (written < n) {
            const ssize_t result = ::write(STDOUT_FILENO, buffer + written, static_cast<std::size_t>(n - written));
            if (result <= 0) {
                break;
            }
            written += result;
        }
    }
    ::shutdown(fd, SHUT_RD);
    stdinToSocket.join();
    ::close(fd);
    return 0;
}

} // namespace ned::editor::mcp
