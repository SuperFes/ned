#include "Watch.h"

#include <utility>
#include <vector>

#include "Editor/DiagnosticsLog.h"
#include "Editor/ProcessTimeouts.h"
#include "Editor/Protocol/Endpoint.h"

namespace ned::editor::remote {

using Json = nlohmann::json;

Watches::Watches(Hosts& hosts, Events onEvents) : hosts_(hosts), onEvents_(std::move(onEvents)) {
}

void Watches::Set(std::map<std::string, WatchSet> wanted) {
    wanted_ = std::move(wanted);
    Resync();
}

void Watches::Resync() {
    std::set<std::string> destinations;
    for (const auto& [destination, set] : wanted_) {
        destinations.insert(destination);
    }
    for (const auto& [destination, told] : told_) {
        destinations.insert(destination);
    }
    for (const std::string& destination : destinations) {
        Sync(destination);
    }
}

void Watches::Sync(const std::string& destination) {
    Connection* connection = hosts_.Find(destination);
    if (connection == nullptr || !connection->Connected() || connection->Hello().capabilities.watch < 1) {
        told_.erase(destination);
        return;
    }
    Told& told = told_[destination];
    if (told.serial != connection->Serial()) {
        told = Told{.serial = connection->Serial(), .sent = std::nullopt};
        connection->Endpoint().HandleNotification(std::string(host::kWatchEvent), lifetime_.Bind([this, destination](const Json& params) {
            host::WatchEvents events;
            try {
                events = host::WatchEventsFromJson(params);
            }
            catch (const protocol::ProtocolError& e) {
                LogMessage(LogCategory::General, LogSeverity::Warning, destination + ": a malformed watch event: " + e.what());
                return;
            }
            if (onEvents_) {
                onEvents_(destination, events);
            }
        }));
    }

    const auto      it = wanted_.find(destination);
    const WatchSet  none;
    const WatchSet& want = it == wanted_.end() ? none : it->second;
    // A fresh connection watches nothing already.
    if (told.sent == want || (!told.sent && want == none)) {
        told.sent = want;
        return;
    }
    told.sent = want;
    const host::WatchSetRequest request{.files       = std::vector(want.files.begin(), want.files.end()),
                                        .directories = std::vector(want.directories.begin(), want.directories.end())};
    connection->Endpoint().Request(std::string(host::kWatchSet), host::ToJson(request),
                                   protocol::ResponseHandlers{[](const Json&) {},
                                                              [destination](const protocol::RemoteError& error) {
                                                                  if (error.code == protocol::error_code::kConnectionClosed) {
                                                                      return; // a reconnect resends it
                                                                  }
                                                                  LogMessage(LogCategory::General, LogSeverity::Warning,
                                                                             destination + ": watching its files failed: " + error.message);
                                                              },
                                                              {}},
                                   ProtocolRequestTimeoutMs());
}

} // namespace ned::editor::remote
