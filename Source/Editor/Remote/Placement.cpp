#include "Placement.h"

#include <exception>
#include <memory>
#include <utility>

#include "Editor/Project/Root.h"
#include "Editor/Remote/Fs.h"
#include "Editor/Remote/Hosts.h"

namespace ned::editor::remote {

namespace {

    using Done = std::shared_ptr<std::function<void(Spawned)>>;

    void StartOnHost(Hosts& hosts, const std::string& destination, RemoteSpawn spawn, const Done& done) {
        Connection* connection = hosts.Find(destination);
        if (connection == nullptr || !connection->Connected()) {
            (*done)(std::unexpected(destination + " is not connected"));
            return;
        }
        try {
            (*done)(SpawnOnHost(*connection, std::move(spawn)));
        }
        catch (const std::exception& e) {
            (*done)(std::unexpected(std::string(e.what())));
        }
    }

} // namespace

void SpawnBeside(Hosts* hosts, const std::optional<text::RemoteFile>& beside, RemoteSpawn spawn, LifetimeToken token,
                 std::function<void(Spawned spawned)> done) {
    if (!beside) {
        Spawned spawned = std::unexpected(std::string());
        try {
            spawned = process::ChildProcess(spawn.argv, process::SpawnOptions{.stderrMode = spawn.stderrMode, .cwd = spawn.cwd, .env = spawn.env});
        }
        catch (const std::exception& e) {
            spawned = std::unexpected(std::string(e.what()));
        }
        if (token.Alive()) {
            done(std::move(spawned));
        }
        return;
    }
    if (hosts == nullptr) {
        if (token.Alive()) {
            done(std::unexpected("no remote host can run " + beside->ToString() + "'s processes"));
        }
        return;
    }
    const Done             shared = std::make_shared<std::function<void(Spawned)>>(std::move(done));
    const text::RemoteFile file   = *beside;
    hosts->Connect(file.destination, token.Bind([hosts, file, spawn = std::move(spawn), token, shared](std::expected<Connection*, std::string> connection) mutable {
        if (!connection) {
            (*shared)(std::unexpected(std::move(connection.error())));
            return;
        }
        if (spawn.cwd) {
            StartOnHost(*hosts, file.destination, std::move(spawn), shared);
            return;
        }
        FindProjectRoot(**connection, host::FsProjectRootRequest{.path = file.path, .detect = AutoDetectProjectRoot()},
                        token.Bind([hosts, file, spawn = std::move(spawn), shared](FsResult<std::filesystem::path> root) mutable {
                            if (!root) {
                                (*shared)(std::unexpected("no project root for " + file.ToString() + ": " + root.error().message));
                                return;
                            }
                            spawn.cwd = std::move(*root);
                            StartOnHost(*hosts, file.destination, std::move(spawn), shared);
                        }));
    }));
}

} // namespace ned::editor::remote
