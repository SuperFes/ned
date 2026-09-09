#include "Transport.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include <unistd.h>

namespace ned::editor::mcp {

namespace {

    // Reads exactly one line (up to and excluding a trailing "\r\n" or "\n"),
    // byte at a time -- mirrors Lsp/Transport.cpp's/Acp/Transport.cpp's own
    // identical ReadLine (same reasoning: byte-at-a-time sidesteps a buffered
    // reader ever over-reading past one message into the next). Returns
    // false on EOF before any byte of a new line was read (a clean "the peer
    // disconnected" signal); throws on a genuine read error. EOF *mid*-line
    // is treated as a malformed final message, not a clean disconnect. The
    // first byte of a message waits unbounded (idle between messages is
    // normal); every byte after that is bounded by stallTimeout.
    bool ReadLine(const process::ChildProcess& child, std::string& line, std::chrono::milliseconds stallTimeout) {
        line.clear();
        bool first = true;
        while (true) {
            const std::chrono::milliseconds waitTimeout = first ? std::chrono::milliseconds(-1) : stallTimeout;
            if (!child.WaitReadable(waitTimeout)) {
                if (first) {
                    return false; // closed connection, not a stall -- see Acp/Transport.cpp's identical guard
                }
                throw std::runtime_error("ned: MCP transport stalled mid-message");
            }
            first                = false;
            char          ch     = 0;
            const ssize_t result = ::read(child.ReadFd(), &ch, 1);
            if (result < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                throw std::runtime_error(std::string("ned: MCP transport read failed: ") + std::strerror(errno));
            }
            if (result == 0) {
                return !line.empty() ? throw std::runtime_error("ned: MCP transport EOF mid-message") : false;
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

} // namespace

Transport::Transport(const std::vector<std::string>& argv) : child_(argv, process::StderrMode::Discard) {
}

Transport::Transport(int readFd, int writeFd, pid_t pid) noexcept : child_(readFd, writeFd, pid) {
}

void Transport::WriteMessage(std::string_view jsonPayload, std::chrono::milliseconds stallTimeout) const {
    child_.WriteAll(jsonPayload, stallTimeout);
    child_.WriteAll("\n", stallTimeout);
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

} // namespace ned::editor::mcp
