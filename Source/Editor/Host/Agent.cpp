#include "Agent.h"

#include <optional>

#include <poll.h>

#include "Editor/Host/Dispatcher.h"
#include "Editor/PostQueue.h"
#include "Editor/Protocol/Cbor.h"
#include "Editor/Protocol/FrameTransport.h"
#include "Editor/Protocol/FramedConnection.h"

namespace ned::editor::host {

std::optional<std::string> RunAgent(int readFd, int writeFd, std::string build) {
    using Connection = protocol::FramedConnection<protocol::FrameTransport>;

    PostQueue                  queue;
    bool                       done = false;
    std::optional<std::string> failure;
    const auto                 fail = [&done, &failure](std::string reason) {
        if (!failure) {
            failure = std::move(reason);
        }
        done = true;
    };

    Connection         connection(protocol::FrameTransport(readFd, writeFd), queue,
                                  Connection::Options{.logCategory        = LogCategory::General,
                                                      .entityNoun         = "editor",
                                                      .disconnectSeverity = LogSeverity::Info});
    protocol::Endpoint endpoint([&connection, &fail](const nlohmann::json& message) {
        std::string frame = protocol::EncodeCbor(message);
        if (frame.size() > protocol::kMaxFrameBytes) {
            fail("ned-agent: a message exceeds the frame cap");
            return;
        }
        connection.SendFrame(std::move(frame));
    });
    const Dispatcher   dispatcher(endpoint, queue, Dispatcher::Identity{.build = std::move(build)},
                                  [&connection](std::string_view codec) { connection.Transport().SetCompression(codec == kCodecZstd); });

    connection.SetOnFrame([&endpoint, &fail](const std::string& frame) {
        try {
            endpoint.Receive(protocol::DecodeCbor(frame));
        }
        catch (const std::exception& e) {
            fail(e.what());
        }
    });
    connection.SetOnDisconnected([&endpoint, &done](const std::string& reason) {
        endpoint.Close(reason);
        done = true;
    });

    while (!done) {
        pollfd wake{.fd = queue.WakeFd(), .events = POLLIN, .revents = 0};
        ::poll(&wake, 1, -1);
        queue.DiscardWakeups();
        queue.DrainPosted_();
    }

    return failure;
}

} // namespace ned::editor::host
