//
// A host's project settings: <root>/.ned/settings.json (Project/Settings.h)
// on that host, read through fs, so what starts there is configured as a
// local project's would be. Nothing connects.
//

#ifndef NED_EDITOR_REMOTE_PROJECTSETTINGS_H
#define NED_EDITOR_REMOTE_PROJECTSETTINGS_H

#include <cstddef>
#include <functional>

#include "Editor/Lifetime.h"
#include "Editor/Project/Settings.h"
#include "Text/RemoteFile.h"

namespace ned::editor::remote {

class Hosts;

// More than any settings file needs.
inline constexpr std::size_t kMaxSettingsBytes = std::size_t{1} << 20;

// done runs once, on the main thread, with what root's settings configure:
// nothing when its host isn't connected, or the file is missing, too big or
// malformed. Not at all once token has died.
void LoadHostProjectSettings(Hosts& hosts, const text::RemoteFile& root, LifetimeToken token, std::function<void(ProjectSettings settings)> done);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_PROJECTSETTINGS_H
