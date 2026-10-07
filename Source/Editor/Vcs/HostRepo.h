//
// A remote file's repository, found on its host: the root fs/project-root
// walks up to, and the provider whose markers that root holds. Detect can't
// run there, so only a provider that names markers is ever found.
//

#ifndef NED_EDITOR_VCS_HOSTREPO_H
#define NED_EDITOR_VCS_HOSTREPO_H

#include <expected>
#include <functional>
#include <string>

#include "Editor/Lifetime.h"
#include "Provider.h"
#include "Repo.h"
#include "Text/RemoteFile.h"

namespace ned::editor::remote {
class Hosts;
} // namespace ned::editor::remote

namespace ned::editor::vcs {

// Main thread only. Neither calls done once token has died. Both connect
// to the host first if need be.

// The repository file is in. Always walks up for a VCS marker, whatever
// Project/Root.h's setting: what matters is the repository, not a project.
void FindRepoOnHost(remote::Hosts& hosts, const text::RemoteFile& file, LifetimeToken token,
                    std::function<void(std::expected<Repo, std::string>)> done);

// The first provider, in registration order, whose markers repo's root
// holds; nullptr when none does. Cached (ProviderRegistry.h), so done runs
// before this returns once it has been found.
void FindProviderOnHost(remote::Hosts& hosts, const Repo& repo, LifetimeToken token,
                        std::function<void(std::expected<Provider*, std::string>)> done);

} // namespace ned::editor::vcs

#endif // NED_EDITOR_VCS_HOSTREPO_H
