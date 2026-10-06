//
// The editor's remote hosts, by ssh destination: at most one live
// Connection each, and at most one attempt in flight. Main thread only.
//

#ifndef NED_EDITOR_REMOTE_HOSTS_H
#define NED_EDITOR_REMOTE_HOSTS_H

#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Editor/Lifetime.h"
#include "Editor/PostQueue.h"
#include "Editor/Remote/Connection.h"
#include "Editor/Remote/HostLookup.h"

namespace ned::editor::remote {

class Hosts {
  public:
    using Connected      = std::function<void(std::expected<Connection*, std::string> connection)>;
    using Dropped        = std::function<void(const std::string& destination, const std::string& reason)>;
    using OptionsFactory = std::function<ConnectOptions(const std::string& destination)>;

    explicit Hosts(PostQueue& postQueue, OptionsFactory options = DefaultOptions, HostLookupOptions lookup = {});
    // Cancels every attempt and closes every connection.
    ~Hosts();

    Hosts(const Hosts&)            = delete;
    Hosts& operator=(const Hosts&) = delete;

    [[nodiscard]] static ConnectOptions DefaultOptions(const std::string& destination);

    // done gets the destination's connection: the live one at once,
    // otherwise once an attempt ends. Callers asking while an attempt is
    // in flight share it.
    void Connect(const std::string& destination, Connected done);
    // Cancels an attempt, whose callers then get the cancellation, or
    // closes a connection, failing its pending requests. Not from inside
    // that connection's own handlers. False if there was nothing to end.
    bool Disconnect(const std::string& destination);

    [[nodiscard]] Connection* Find(const std::string& destination) const;
    [[nodiscard]] bool        Connecting(const std::string& destination) const;
    // Live connections, sorted.
    [[nodiscard]] std::vector<std::string> Destinations() const;

    // Runs when a live connection ends without Disconnect: the agent
    // exited, the link dropped, or the agent broke the protocol.
    void SetOnDropped(Dropped handler);

    // Resolver lookups for a connect prompt's host names.
    [[nodiscard]] HostLookup& Lookup() noexcept;

  private:
    struct Attempt {
        std::unique_ptr<Connector> connector;
        std::vector<Connected>     waiters;
    };

    void Finish(const std::string& destination, Connector::Result result);

    PostQueue&                                         postQueue_;
    OptionsFactory                                     options_;
    Dropped                                            onDropped_;
    std::map<std::string, std::unique_ptr<Connection>> connections_;
    std::map<std::string, Attempt>                     attempts_;
    HostLookup                                         lookup_;
    LifetimeGuard                                      lifetime_;
};

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_HOSTS_H
