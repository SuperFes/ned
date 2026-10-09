#include "CommandLine.h"

#include "Editor/Remote/Fs.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/Remote/Location.h"

namespace ned::editor::remote {

std::optional<text::RemoteFile> RemoteArgument(std::string_view argument) {
    std::optional<text::RemoteFile> file = ParsePromptLocation(argument);
    if (!file || file->destination.find('/') != std::string::npos) {
        return std::nullopt;
    }
    return file;
}

void ClassifyRemoteArgument(Hosts& hosts, const text::RemoteFile& argument, LifetimeToken token,
                            std::function<void(std::expected<RemoteArgumentKind, std::string> kind)> done) {
    hosts.Connect(argument.destination, token.Bind([argument, token, done = std::move(done)](const std::expected<Connection*, std::string>& connection) {
        if (!connection) {
            done(std::unexpected(connection.error()));
            return;
        }
        (void)StatFile(**connection, argument.path, token.Bind([done](const FsResult<host::FileStat>& stat) {
            if (!stat && stat.error().code != host::fs_error::kNotFound) {
                done(std::unexpected(stat.error().message));
                return;
            }
            done(stat && stat->kind == host::FileKind::Directory ? RemoteArgumentKind::Directory : RemoteArgumentKind::File);
        }));
    }));
}

} // namespace ned::editor::remote
