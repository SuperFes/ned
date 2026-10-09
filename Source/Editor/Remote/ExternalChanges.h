//
// What a host's watch events mean for the buffers holding its files: the
// remote half of AutoRevert.h and AutoMerge.h, driven by events rather than
// by a sweep, since a remote stat costs a round trip.
//
// A buffer whose file an event names is stat'd on its host, and nothing
// more happens unless the file's timestamp moved on from the one the buffer
// last read or wrote, so the buffer's own saves stay quiet. A changed file
// is then read and, deciding against the buffer as it is once the read
// lands, reverted into an unmodified buffer, merged into a modified one, or,
// with the matching toggle off, only noted so the next save asks first.
//

#ifndef NED_EDITOR_REMOTE_EXTERNALCHANGES_H
#define NED_EDITOR_REMOTE_EXTERNALCHANGES_H

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "Editor/Host/WatchCodec.h"
#include "Editor/Lifetime.h"
#include "Editor/Remote/Hosts.h"
#include "Text/BufferList.h"

namespace ned::editor::remote {

struct RemoteChange {
    enum class Outcome { Reverted,
                         Merged,
                         Noted };

    std::string name;
    Outcome     outcome   = Outcome::Noted;
    std::size_t conflicts = 0;
};

// Moves each open remote buffer on destination whose file, or a directory
// above it, moved there to where it went, as a local buffer follows a move
// on disk. The buffers that moved.
std::vector<text::Buffer*> FollowRemoteMoves(text::BufferList& buffers, const std::string& destination, const std::vector<FileMove>& moves);

// Checks every open remote buffer on destination that events names, or
// every one there when the host lost events. Nothing connects: a host
// that has gone away is skipped. report runs on the main thread once per
// buffer that changed.
void FollowRemoteChanges(Hosts& hosts, text::BufferList& buffers, const std::string& destination, const host::WatchEvents& events,
                         LifetimeToken token, std::function<void(const RemoteChange& change)> report);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_EXTERNALCHANGES_H
