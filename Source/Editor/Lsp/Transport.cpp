#include "Transport.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <unistd.h>

namespace ned::editor::lsp {

namespace {

    // Reads exactly one line (up to and excluding a trailing "\r\n" or "\n"),
    // byte at a time -- headers are two short lines per frame, so the extra
    // syscalls are negligible; a buffered reader would have to be careful
    // never to over-read past the header/body boundary, which byte-at-a-time
    // reading sidesteps for free. Returns false on EOF before any byte of a
    // new line was read (a clean "the server exited" signal); throws on a
    // genuine read error.
    bool ReadLine(int fd, std::string& line) {
        line.clear();
        while (true) {
            char          ch     = 0;
            const ssize_t result = ::read(fd, &ch, 1);
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(std::string("ned: LSP transport read failed: ") + std::strerror(errno));
            }
            if (result == 0) {
                return !line.empty() ? throw std::runtime_error("ned: LSP transport EOF mid-header-line") : false;
            }
            if (ch == '\n') {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                return true;
            }
            line += ch;
        }
    }

    // Reads exactly n bytes into buffer, looping over ::read() for partial
    // reads/EINTR. Returns false on EOF before n bytes were read.
    bool ReadExact(int fd, char* buffer, std::size_t n) {
        std::size_t got = 0;
        while (got < n) {
            const ssize_t result = ::read(fd, buffer + got, n - got);
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(std::string("ned: LSP transport read failed: ") + std::strerror(errno));
            }
            if (result == 0) {
                return false;
            }
            got += static_cast<std::size_t>(result);
        }
        return true;
    }

} // namespace

Transport::Transport(const std::vector<std::string>& argv) : child_(argv, process::StderrMode::Discard) {
}

Transport::Transport(int readFd, int writeFd, pid_t pid) noexcept : child_(readFd, writeFd, pid) {
}

void Transport::WriteFrame(std::string_view jsonPayload) const {
    const std::string header = "Content-Length: " + std::to_string(jsonPayload.size()) + "\r\n\r\n";
    child_.WriteAll(header);
    child_.WriteAll(jsonPayload);
}

std::optional<std::string> Transport::ReadFrame() const {
    std::size_t contentLength    = 0;
    bool        sawContentLength = false;
    const int   fd               = child_.ReadFd();

    while (true) {
        std::string line;
        if (!ReadLine(fd, line)) {
            return std::nullopt; // EOF before any header -- clean "server exited" between frames
        }
        if (line.empty()) {
            break; // blank line terminates the header block
        }
        constexpr std::string_view kPrefix = "Content-Length: ";
        if (line.rfind(kPrefix, 0) == 0) {
            try {
                contentLength    = std::stoul(line.substr(kPrefix.size()));
                sawContentLength = true;
            }
            catch (const std::exception&) {
                throw std::runtime_error("ned: LSP frame has an unparseable Content-Length header: " + line);
            }
        }
        // Any other header (e.g. Content-Type) is ignored, per the LSP spec.
    }

    if (!sawContentLength) {
        throw std::runtime_error("ned: LSP frame missing Content-Length header");
    }

    std::string body(contentLength, '\0');
    if (!ReadExact(fd, body.data(), contentLength)) {
        return std::nullopt; // EOF mid-body -- server exited mid-message
    }
    return body;
}

pid_t Transport::Pid() const noexcept {
    return child_.Pid();
}

} // namespace ned::editor::lsp
