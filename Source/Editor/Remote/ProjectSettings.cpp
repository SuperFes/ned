#include "ProjectSettings.h"

#include <utility>

#include "Editor/Remote/Fs.h"
#include "Editor/Remote/Hosts.h"

namespace ned::editor::remote {

void LoadHostProjectSettings(Hosts& hosts, const text::RemoteFile& root, LifetimeToken token, std::function<void(ProjectSettings settings)> done) {
    Connection* connection = hosts.Find(root.destination);
    if (connection == nullptr || !connection->Connected()) {
        if (token.Alive()) {
            done(ProjectSettings{});
        }
        return;
    }
    (void)ReadFile(*connection, root.path / ".ned" / "settings.json", kMaxSettingsBytes,
                   token.Bind([path = root.path, done = std::move(done)](const FsResult<text::FileContents>& read) {
                       done(read ? ParseProjectSettings(read->bytes, path) : ProjectSettings{});
                   }));
}

} // namespace ned::editor::remote
