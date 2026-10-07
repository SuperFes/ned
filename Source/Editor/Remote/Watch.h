//
// The editor's half of the watch namespace (Host/WatchCodec.h): what each
// host is asked to watch, kept in step with its connection, and the host's
// events handed back. Nothing here connects; a host is told what to watch
// once it is connected, and again after every reconnect. Main thread only.
//

#ifndef NED_EDITOR_REMOTE_WATCH_H
#define NED_EDITOR_REMOTE_WATCH_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "Editor/Host/WatchCodec.h"
#include "Editor/Lifetime.h"
#include "Editor/Remote/Hosts.h"

namespace ned::editor::remote {

struct WatchSet {
    std::set<std::filesystem::path> files;
    std::set<std::filesystem::path> directories;

    [[nodiscard]] bool operator==(const WatchSet&) const = default;
};

class Watches {
  public:
    using Events = std::function<void(const std::string& destination, const host::WatchEvents& events)>;

    // hosts must outlive this.
    Watches(Hosts& hosts, Events onEvents);

    Watches(const Watches&)            = delete;
    Watches& operator=(const Watches&) = delete;

    // What each host should watch, by destination; a destination left out
    // watches nothing. Connected hosts whose set changed are told at once.
    void Set(std::map<std::string, WatchSet> wanted);
    // Tells hosts that connected since; wire to Hosts::SetOnChanged.
    void Resync();

  private:
    struct Told {
        std::uint64_t           serial = 0; // the Connection told
        std::optional<WatchSet> sent;
    };

    void Sync(const std::string& destination);

    Hosts&                          hosts_;
    Events                          onEvents_;
    std::map<std::string, WatchSet> wanted_;
    std::map<std::string, Told>     told_;
    LifetimeGuard                   lifetime_;
};

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_WATCH_H
