#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "Editor/Dap/DapClient.h"
#include "Editor/Lsp/Transport.h"
#include "UI/EventLoop.h"

using ned::editor::dap::DapClient;
using ned::editor::dap::Json;
using ned::editor::lsp::Transport;

namespace {

// Same pipe-pair fixture shape as LspClientTest's ClientFixture -- see that
// file's own extensive comment for why serverStdoutWrite must be closed
// before the client (its background read thread's blocking read() only
// returns on EOF, which needs every write-end reference gone) and why the
// owned-by-value EventLoop is constructed per-fixture rather than shared.
struct ClientFixture {
    ned::ui::EventLoop eventLoop;
    int                adapterStdinRead;
    int                adapterStdoutWrite;
    DapClient          client;

    ClientFixture(int readFd, int writeFd, Transport transport) : adapterStdinRead(readFd), adapterStdoutWrite(writeFd), client(std::move(transport), eventLoop) {
    }

    ~ClientFixture() {
        ::close(adapterStdoutWrite);
        ::close(adapterStdinRead);
    }

    ClientFixture(const ClientFixture&)            = delete;
    ClientFixture& operator=(const ClientFixture&) = delete;

    static ClientFixture Create() {
        int clientWritesHere[2];
        int clientReadsHere[2];
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        return ClientFixture(clientWritesHere[0], clientReadsHere[1], Transport(clientReadsHere[0], clientWritesHere[1]));
    }
};

// Reads exactly one Content-Length frame's body off a plain fd, buffering
// any bytes belonging to a following frame for the next call -- unlike
// LspClientTest's simpler ReadRawFrame, DAP flows legitimately write two
// frames back to back (setBreakpoints + configurationDone), so leftover
// bytes must survive between calls.
struct FrameReader {
    int         fd;
    std::string buffer;

    Json Next() {
        for (int i = 0; i < 16; ++i) {
            const auto headerEnd = buffer.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                const std::string_view kPrefix   = "Content-Length: ";
                const auto             prefixPos = buffer.find(kPrefix);
                REQUIRE(prefixPos != std::string::npos);
                const std::size_t contentLength = std::stoul(buffer.substr(prefixPos + kPrefix.size()));
                if (buffer.size() >= headerEnd + 4 + contentLength) {
                    const std::string body = buffer.substr(headerEnd + 4, contentLength);
                    buffer.erase(0, headerEnd + 4 + contentLength);
                    return Json::parse(body);
                }
            }
            char          chunk[512];
            const ssize_t n = ::read(fd, chunk, sizeof(chunk));
            if (n <= 0) {
                break;
            }
            buffer.append(chunk, static_cast<std::size_t>(n));
        }
        FAIL("no complete frame available on fd");
        return Json::object();
    }
};

std::string ResponseFrame(int requestSeq, const std::string& command, bool success, Json body = Json::object(),
                          const std::string& message = "") {
    Json response = {
        {"seq", 1000 + requestSeq},
        {"type", "response"},
        {"request_seq", requestSeq},
        {"command", command},
        {"success", success},
    };
    if (success) {
        response["body"] = std::move(body);
    }
    else {
        response["message"] = message;
    }
    return response.dump();
}

} // namespace

TEST_CASE("DapClient::SendRequest writes a well-formed DAP request frame", "[Dap]") {
    ClientFixture fixture = ClientFixture::Create();

    fixture.client.SendRequest("initialize", Json{{"adapterID", "test"}}, [](bool, Json, std::string) {});

    FrameReader reader{fixture.adapterStdinRead};
    const Json  message = reader.Next();
    REQUIRE(message["type"] == "request");
    REQUIRE(message["command"] == "initialize");
    REQUIRE(message["seq"].is_number_integer());
    REQUIRE(message["arguments"]["adapterID"] == "test");
    // Deliberately NOT JSON-RPC: no jsonrpc/id/method keys anywhere.
    REQUIRE_FALSE(message.contains("jsonrpc"));
    REQUIRE_FALSE(message.contains("method"));
}

TEST_CASE("DapClient correlates a success response to its request callback by request_seq", "[Dap]") {
    ClientFixture fixture = ClientFixture::Create();

    bool called = false;
    bool ok     = false;
    Json body;
    fixture.client.SendRequest("initialize", Json::object(), [&](bool success, Json responseBody, std::string) {
        called = true;
        ok     = success;
        body   = std::move(responseBody);
    });

    FrameReader reader{fixture.adapterStdinRead};
    const int   seq = reader.Next()["seq"].get<int>();

    fixture.client.DispatchFrame(ResponseFrame(seq, "initialize", true, Json{{"supportsConfigurationDoneRequest", true}}));
    REQUIRE(called);
    REQUIRE(ok);
    REQUIRE(body["supportsConfigurationDoneRequest"] == true);
}

TEST_CASE("DapClient::ExpireStaleRequests resolves a stuck request with success=false", "[Dap]") {
    // subprocess-hang-protection follow-up.
    ClientFixture fixture = ClientFixture::Create();

    bool        invoked = false;
    bool        success = true;
    std::string message;
    fixture.client.SendRequest("evaluate", Json::object(), [&](bool ok, Json, std::string msg) {
        invoked = true;
        success = ok;
        message = std::move(msg);
    });
    REQUIRE_FALSE(invoked);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    fixture.client.ExpireStaleRequests(std::chrono::milliseconds(1));

    REQUIRE(invoked);
    REQUIRE_FALSE(success);
    REQUIRE_FALSE(message.empty());
}

TEST_CASE("DapClient::ExpireStaleRequests leaves a request younger than maxAge untouched", "[Dap]") {
    // subprocess-hang-protection follow-up.
    ClientFixture fixture = ClientFixture::Create();

    bool invoked = false;
    fixture.client.SendRequest("evaluate", Json::object(), [&](bool, Json, std::string) { invoked = true; });

    fixture.client.ExpireStaleRequests(std::chrono::milliseconds(60000));

    REQUIRE_FALSE(invoked);
}

TEST_CASE("DapClient reports a failed response's own message", "[Dap]") {
    ClientFixture fixture = ClientFixture::Create();

    std::string message;
    bool        ok = true;
    fixture.client.SendRequest("launch", Json::object(), [&](bool success, Json, std::string errorMessage) {
        ok      = success;
        message = std::move(errorMessage);
    });

    FrameReader reader{fixture.adapterStdinRead};
    const int   seq = reader.Next()["seq"].get<int>();

    fixture.client.DispatchFrame(ResponseFrame(seq, "launch", false, Json::object(), "program not found"));
    REQUIRE_FALSE(ok);
    REQUIRE(message == "program not found");
}

TEST_CASE("DapClient invokes each response callback at most once and ignores unknown request_seqs", "[Dap]") {
    ClientFixture fixture = ClientFixture::Create();

    int calls = 0;
    fixture.client.SendRequest("initialize", Json::object(), [&](bool, Json, std::string) { ++calls; });

    FrameReader reader{fixture.adapterStdinRead};
    const int   seq = reader.Next()["seq"].get<int>();

    fixture.client.DispatchFrame(ResponseFrame(seq, "initialize", true));
    fixture.client.DispatchFrame(ResponseFrame(seq, "initialize", true));     // duplicate -- already handled
    fixture.client.DispatchFrame(ResponseFrame(seq + 7, "initialize", true)); // never sent
    REQUIRE(calls == 1);
}

TEST_CASE("DapClient dispatches events to their registered handler with the event body", "[Dap]") {
    ClientFixture fixture = ClientFixture::Create();

    std::string reason;
    fixture.client.SetEventHandler("stopped", [&](const Json& body) { reason = body.value("reason", ""); });

    fixture.client.DispatchFrame(Json{
        {"seq", 5},
        {"type", "event"},
        {"event", "stopped"},
        {"body", Json{{"reason", "breakpoint"}, {"threadId", 1}}},
    }
                                     .dump());
    REQUIRE(reason == "breakpoint");

    // An event with no registered handler (and a bodyless event) are both
    // ignored without incident.
    fixture.client.DispatchFrame(Json{{"seq", 6}, {"type", "event"}, {"event", "unheard-of"}}.dump());
}

TEST_CASE("DapClient ignores malformed frames rather than crashing", "[Dap]") {
    ClientFixture fixture = ClientFixture::Create();
    fixture.client.DispatchFrame("this is not json");
    fixture.client.DispatchFrame(R"({"type": "response"})");                       // no request_seq
    fixture.client.DispatchFrame(R"({"type": "response", "request_seq": "nan"})"); // wrong type
}
