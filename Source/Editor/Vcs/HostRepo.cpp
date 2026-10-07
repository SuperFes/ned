#include "HostRepo.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "Editor/Remote/Fs.h"
#include "Editor/Remote/Hosts.h"
#include "ProviderRegistry.h"

namespace ned::editor::vcs {

namespace {

    struct Candidate {
        std::string name;
        std::string marker;
    };

    struct Search {
        remote::Hosts&         hosts;
        Repo                   repo;
        std::vector<Candidate> candidates;
        LifetimeToken          token;

        std::function<void(std::expected<Provider*, std::string>)> done;
    };

    // Stats candidates in turn from next; a connection lost partway is an
    // error, not a miss, so it isn't cached as one.
    void Probe(const std::shared_ptr<Search>& search, std::size_t next) {
        if (next == search->candidates.size()) {
            CacheHostProvider(*search->repo.destination, search->repo.root, nullptr);
            search->done(nullptr);
            return;
        }
        remote::Connection* connection = search->hosts.Find(*search->repo.destination);
        if (connection == nullptr || !connection->Connected()) {
            search->done(std::unexpected(*search->repo.destination + " is not connected"));
            return;
        }
        const Candidate& candidate = search->candidates[next];
        (void)remote::StatFile(*connection, search->repo.root / candidate.marker,
                               search->token.Bind([search, next](remote::FsResult<host::FileStat> stat) {
                                   if (!stat) {
                                       search->done(std::unexpected("can't look for " + search->repo.ToString() + "'s provider: " + stat.error().message));
                                       return;
                                   }
                                   if (stat->kind == host::FileKind::Missing) {
                                       Probe(search, next + 1);
                                       return;
                                   }
                                   Provider* provider = ProviderNamed(search->candidates[next].name);
                                   CacheHostProvider(*search->repo.destination, search->repo.root, provider);
                                   search->done(provider);
                               }));
    }

} // namespace

void FindRepoOnHost(remote::Hosts& hosts, const text::RemoteFile& file, LifetimeToken token,
                    std::function<void(std::expected<Repo, std::string>)> done) {
    auto shared = std::make_shared<std::function<void(std::expected<Repo, std::string>)>>(std::move(done));
    hosts.Connect(file.destination, token.Bind([file, token, shared](std::expected<remote::Connection*, std::string> connection) {
        if (!connection) {
            (*shared)(std::unexpected(std::move(connection.error())));
            return;
        }
        (void)remote::FindProjectRoot(**connection, host::FsProjectRootRequest{.path = file.path, .detect = true},
                                      token.Bind([file, shared](remote::FsResult<std::filesystem::path> root) {
                                          if (!root) {
                                              (*shared)(std::unexpected("no repository for " + file.ToString() + ": " + root.error().message));
                                              return;
                                          }
                                          (*shared)(Repo{.destination = file.destination, .root = std::move(*root)});
                                      }));
    }));
}

void FindProviderOnHost(remote::Hosts& hosts, const Repo& repo, LifetimeToken token,
                        std::function<void(std::expected<Provider*, std::string>)> done) {
    if (const std::optional<Provider*> cached = CachedHostProvider(*repo.destination, repo.root)) {
        if (token.Alive()) {
            done(*cached);
        }
        return;
    }
    auto search = std::make_shared<Search>(Search{.hosts = hosts, .repo = repo, .candidates = {}, .token = token, .done = std::move(done)});
    for (MarkedProvider& marked : MarkedProviders()) {
        for (std::string& marker : marked.markers) {
            search->candidates.push_back(Candidate{.name = marked.name, .marker = std::move(marker)});
        }
    }
    hosts.Connect(*repo.destination, token.Bind([search](std::expected<remote::Connection*, std::string> connection) {
        if (!connection) {
            search->done(std::unexpected(std::move(connection.error())));
            return;
        }
        Probe(search, 0);
    }));
}

} // namespace ned::editor::vcs
