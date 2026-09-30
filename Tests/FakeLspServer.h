//
// A raw pipe pair standing in for a language server's stdin/stdout, handed
// to Manager::SetClientForTesting. The test reads what the client wrote off
// serverStdinRead and answers by calling Client::DispatchFrame directly.
//

#ifndef NED_TESTS_FAKELSPSERVER_H
#define NED_TESTS_FAKELSPSERVER_H

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <unistd.h>

#include "BoundedRead.h"
#include "Editor/Lsp/Client.h"
#include "Editor/Lsp/Manager.h"
#include "Editor/Lsp/Transport.h"
#include "UI/EventLoop.h"

namespace ned::test {

struct FakeLspServer {
    int serverStdinRead   = -1;
    int serverStdoutWrite = -1;

    FakeLspServer(int readFd, int writeFd) : serverStdinRead(readFd), serverStdoutWrite(writeFd) {
    }

    ~FakeLspServer() {
        if (serverStdoutWrite >= 0) {
            ::close(serverStdoutWrite);
        }
        if (serverStdinRead >= 0) {
            ::close(serverStdinRead);
        }
    }

    FakeLspServer(const FakeLspServer&)            = delete;
    FakeLspServer& operator=(const FakeLspServer&) = delete;

    // Not defaulted: a defaulted move would leave both objects owning the fds.
    FakeLspServer(FakeLspServer&& other) noexcept
        : serverStdinRead(std::exchange(other.serverStdinRead, -1)), serverStdoutWrite(std::exchange(other.serverStdoutWrite, -1)),
          pending_(std::move(other.pending_)) {
    }

    static FakeLspServer Create(editor::lsp::Manager& manager, const std::string& language, ui::EventLoop& eventLoop,
                                editor::lsp::Client*& outClient) {
        int clientWritesHere[2];
        int clientReadsHere[2];
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        auto client = std::make_unique<editor::lsp::Client>(editor::lsp::Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop);
        outClient   = &manager.SetClientForTesting(language, std::move(client));
        return FakeLspServer(clientWritesHere[0], clientReadsHere[1]);
    }

    // Blocks until one whole frame has arrived and returns its JSON body.
    // Bytes past that frame are kept for the next call, since two frames
    // written back to back can arrive in one read.
    [[nodiscard]] editor::lsp::Json ReadFrame() {
        char chunk[512];
        while (true) {
            const std::size_t headerEnd = pending_.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                const std::string_view kPrefix       = "Content-Length: ";
                const std::size_t      contentLength = std::stoul(pending_.substr(pending_.find(kPrefix) + kPrefix.size()));
                const std::size_t      frameEnd      = headerEnd + 4 + contentLength;
                if (pending_.size() >= frameEnd) {
                    editor::lsp::Json body = editor::lsp::Json::parse(pending_.substr(headerEnd + 4, contentLength));
                    pending_.erase(0, frameEnd);
                    return body;
                }
            }
            const ssize_t n = ned::test::BoundedRead(serverStdinRead, chunk, sizeof(chunk));
            REQUIRE(n > 0);
            pending_.append(chunk, static_cast<std::size_t>(n));
        }
    }

    // Reads frames until one carries `method`, skipping notifications such
    // as didOpen/didChange that the client sends along the way.
    [[nodiscard]] editor::lsp::Json ReadRequest(const std::string& method) {
        while (true) {
            editor::lsp::Json frame = ReadFrame();
            if (frame.value("method", std::string()) == method) {
                return frame;
            }
        }
    }

  private:
    std::string pending_;
};

} // namespace ned::test

#endif // NED_TESTS_FAKELSPSERVER_H
