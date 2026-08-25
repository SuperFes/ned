#include "Transport.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include <unistd.h>

namespace ned::editor::lsp {

namespace {

    // lsp-stderr-capture follow-up: argv[0]'s basename, for ProcessLabel().
    std::string BaseName(const std::string& path) {
        const std::size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // Reads exactly one line (up to and excluding a trailing "\r\n" or "\n"),
    // byte at a time -- headers are two short lines per frame, so the extra
    // syscalls are negligible; a buffered reader would have to be careful
    // never to over-read past the header/body boundary, which byte-at-a-time
    // reading sidesteps for free. Returns false on EOF before any byte of a
    // new line was read (a clean "the server exited" signal); throws on a
    // genuine read error. waitFirstByteUnbounded must be true only for the
    // very first line of a fresh frame -- an idle connection between
    // messages is normal and must never be mistaken for a stall; every other
    // byte (including this call's own 2nd+ byte, when false) is bounded by
    // stallTimeout.
    bool ReadLine(const process::ChildProcess& child, std::string& line, bool waitFirstByteUnbounded,
                  std::chrono::milliseconds stallTimeout) {
        line.clear();
        bool first = true;
        while (true) {
            if (!(first && waitFirstByteUnbounded) && !child.WaitReadable(stallTimeout)) {
                throw std::runtime_error("ned: LSP transport stalled mid-frame");
            }
            first                = false;
            char          ch     = 0;
            const ssize_t result = ::read(child.ReadFd(), &ch, 1);
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
    // reads/EINTR. Returns false on EOF before n bytes were read. Always
    // called for a frame's body, i.e. always after headers have already
    // started arriving -- every byte is bounded by stallTimeout, no
    // unbounded-first-byte exception the way ReadLine has.
    bool ReadExact(const process::ChildProcess& child, char* buffer, std::size_t n, std::chrono::milliseconds stallTimeout) {
        std::size_t got = 0;
        while (got < n) {
            if (!child.WaitReadable(stallTimeout)) {
                throw std::runtime_error("ned: LSP transport stalled mid-frame");
            }
            const ssize_t result = ::read(child.ReadFd(), buffer + got, n - got);
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

Transport::Transport(const std::vector<std::string>& argv, bool captureStderr)
    : child_(argv, captureStderr ? process::StderrMode::Capture : process::StderrMode::Discard),
      processLabel_(argv.empty() ? std::string() : BaseName(argv[0])) {
}

Transport::Transport(int readFd, int writeFd, pid_t pid) noexcept : child_(readFd, writeFd, pid) {
}

void Transport::WriteFrame(std::string_view jsonPayload) const {
    const std::string header = "Content-Length: " + std::to_string(jsonPayload.size()) + "\r\n\r\n";
    child_.WriteAll(header);
    child_.WriteAll(jsonPayload);
}

std::optional<std::string> Transport::ReadFrame(std::chrono::milliseconds stallTimeout) const {
    std::size_t contentLength    = 0;
    bool        sawContentLength = false;
    bool        firstLine        = true; // only this call gets an unbounded wait for its first byte -- see ReadLine's own doc comment

    while (true) {
        std::string line;
        if (!ReadLine(child_, line, firstLine, stallTimeout)) {
            return std::nullopt; // EOF before any header -- clean "server exited" between frames
        }
        firstLine = false;
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
    if (!ReadExact(child_, body.data(), contentLength, stallTimeout)) {
        return std::nullopt; // EOF mid-body -- server exited mid-message
    }
    return body;
}

pid_t Transport::Pid() const noexcept {
    return child_.Pid();
}

int Transport::StderrFd() const noexcept {
    return child_.StderrFd();
}

const std::string& Transport::ProcessLabel() const noexcept {
    return processLabel_;
}

} // namespace ned::editor::lsp
