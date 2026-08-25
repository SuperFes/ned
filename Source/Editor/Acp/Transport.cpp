#include "Transport.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include <unistd.h>

namespace ned::editor::acp {

namespace {

    // Reads exactly one line (up to and excluding a trailing "\r\n" or "\n"),
    // byte at a time -- mirrors Lsp/Transport.cpp's own ReadLine exactly
    // (same reasoning: a buffered reader would have to be careful never to
    // over-read past one message into the next, which byte-at-a-time reading
    // sidesteps for free; messages here are typically small enough that the
    // extra syscalls don't matter). Returns false on EOF before any byte of
    // a new line was read (a clean "the agent exited" signal); throws on a
    // genuine read error. EOF *mid*-line (some bytes read, then EOF with no
    // terminating '\n') is treated as a malformed final message, not a clean
    // disconnect -- matches Transport::ReadFrame's own "EOF mid-body" case.
    // The first byte of a message waits unbounded (idle between messages is
    // normal); every byte after that is bounded by stallTimeout -- once a
    // message has started arriving, further silence is anomalous.
    bool ReadLine(const process::ChildProcess& child, std::string& line, std::chrono::milliseconds stallTimeout) {
        line.clear();
        bool first = true;
        while (true) {
            if (!first && !child.WaitReadable(stallTimeout)) {
                throw std::runtime_error("ned: ACP transport stalled mid-message");
            }
            first                = false;
            char          ch     = 0;
            const ssize_t result = ::read(child.ReadFd(), &ch, 1);
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(std::string("ned: ACP transport read failed: ") + std::strerror(errno));
            }
            if (result == 0) {
                return !line.empty() ? throw std::runtime_error("ned: ACP transport EOF mid-message") : false;
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

    // lsp-stderr-capture follow-up (extended to ACP): argv[0]'s basename, for
    // ProcessLabel() -- mirrors Lsp/Transport.cpp's identical BaseName.
    std::string BaseName(const std::string& path) {
        const std::size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

} // namespace

Transport::Transport(const std::vector<std::string>& argv, bool captureStderr)
    : child_(argv, captureStderr ? process::StderrMode::Capture : process::StderrMode::Discard),
      processLabel_(argv.empty() ? std::string() : BaseName(argv[0])) {
}

Transport::Transport(int readFd, int writeFd, pid_t pid) noexcept : child_(readFd, writeFd, pid) {
}

void Transport::WriteMessage(std::string_view jsonPayload) const {
    child_.WriteAll(jsonPayload);
    child_.WriteAll("\n");
}

std::optional<std::string> Transport::ReadMessage(std::chrono::milliseconds stallTimeout) const {
    std::string line;
    if (!ReadLine(child_, line, stallTimeout)) {
        return std::nullopt;
    }
    return line;
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

} // namespace ned::editor::acp
