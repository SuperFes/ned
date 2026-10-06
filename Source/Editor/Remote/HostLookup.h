//
// Which names the system's resolver knows, for offering hosts nothing on
// this machine has connected to yet. Lookups go through `getent ahosts`
// rather than getaddrinfo, which can't be interrupted: a lookup stuck on an
// unreachable server is killed, never waited out.
//

#ifndef NED_EDITOR_REMOTE_HOSTLOOKUP_H
#define NED_EDITOR_REMOTE_HOSTLOOKUP_H

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "Editor/Lifetime.h"
#include "Editor/PostQueue.h"

namespace ned::editor::remote {

struct HostLookupOptions {
    std::filesystem::path    resolvConf = "/etc/resolv.conf";
    std::vector<std::string> command    = {"getent", "ahosts"}; // the name is appended
    // Quiet time before a lookup starts, so typing doesn't query per key.
    std::chrono::milliseconds delay{250};
    // A name still unanswered by then counts as unresolved.
    std::chrono::milliseconds limit{3000};
};

// Main thread only.
class HostLookup {
  public:
    using Done = std::function<void(std::vector<std::string> resolved)>;

    HostLookup(PostQueue& postQueue, HostLookupOptions options = {});
    // Kills any lookup in flight.
    ~HostLookup();

    HostLookup(const HostLookup&)            = delete;
    HostLookup& operator=(const HostLookup&) = delete;

    // Replaces the lookup in flight. done runs on the main thread with the
    // names that resolved, in the order given, unless the lookup is
    // replaced or cancelled first.
    void Lookup(std::vector<std::string> names, Done done);
    // Lookup of typed under each of resolv.conf's search domains; done gets
    // nothing without any.
    void Qualify(const std::string& typed, Done done);
    void Cancel();

  private:
    PostQueue&        postQueue_;
    HostLookupOptions options_;
    LifetimeGuard     lifetime_;
    std::jthread      worker_;
};

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_HOSTLOOKUP_H
