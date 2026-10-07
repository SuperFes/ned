//
// The host side of the protocol: answers an Endpoint's requests against the
// machine this process runs on. ned-agent runs one over stdio; the editor's
// local host will run the same one in process.
//
// The agent only executes (Docs/ClientServerProtocol.md, section 6): every
// trust decision is the editor's, so nothing here asks whether to.
//

#ifndef NED_EDITOR_HOST_DISPATCHER_H
#define NED_EDITOR_HOST_DISPATCHER_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/FileWatch.h"
#include "Editor/Host/Hello.h"
#include "Editor/Lifetime.h"
#include "Editor/PostQueue.h"
#include "Editor/Project/SearchEngine.h"
#include "Editor/Protocol/Endpoint.h"

namespace ned::editor::host {

class Dispatcher {
  public:
    struct Identity {
        std::string build;
        std::string arch = BuildArch();
    };

    // onCodec runs once hello has settled the codec, for the owner to apply
    // to its transport. Both references must outlive this object.
    Dispatcher(protocol::Endpoint& endpoint, PostQueue& postQueue, Identity identity,
               std::function<void(std::string_view codec)> onCodec = {});
    // Stops running searches and the watcher, and waits for them.
    ~Dispatcher();

    Dispatcher(const Dispatcher&)            = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

  private:
    struct SearchJob;
    struct WorkerJob;

    // work runs on a worker; the answer it returns runs back on this thread.
    using WorkerAnswer = std::function<void(protocol::Responder& responder)>;
    void RunOnWorker(protocol::Responder responder, std::function<WorkerAnswer()> work);

    void Hello(const nlohmann::json& params, protocol::Responder responder);
    void SearchProject(const nlohmann::json& params, protocol::Responder responder);
    void SearchMatches(std::uint64_t job, std::vector<SearchMatch> matches);
    void SendPending(SearchJob& job);
    void FinishSearch(std::uint64_t job, const std::string& error);
    void FsStat(const nlohmann::json& params, protocol::Responder responder);
    void FsRead(const nlohmann::json& params, protocol::Responder responder);
    void FsWrite(const nlohmann::json& params, protocol::Responder responder);
    void FsReaddir(const nlohmann::json& params, protocol::Responder responder);
    void FsRealpath(const nlohmann::json& params, protocol::Responder responder);
    void FsMkdir(const nlohmann::json& params, protocol::Responder responder);
    void FsRename(const nlohmann::json& params, protocol::Responder responder);
    void FsRemove(const nlohmann::json& params, protocol::Responder responder);
    void WatchSet(const nlohmann::json& params, protocol::Responder responder);
    // One burst from the watcher, as the editor named the paths; empty when
    // the watcher lost events.
    void ReportWatchEvents(const std::vector<FileEvent>& burst);

    protocol::Endpoint&                         endpoint_;
    PostQueue&                                  postQueue_;
    Identity                                    identity_;
    std::function<void(std::string_view codec)> onCodec_;

    std::uint64_t                                                 nextJob_ = 0;
    std::unordered_map<std::uint64_t, std::unique_ptr<SearchJob>> searches_;
    std::unordered_map<std::uint64_t, std::unique_ptr<WorkerJob>> workerJobs_;

    // watch/set's paths, keyed by the canonical paths the watcher reports.
    std::map<std::filesystem::path, std::vector<std::filesystem::path>> watchedFiles_;
    std::map<std::filesystem::path, std::vector<std::filesystem::path>> watchedDirectories_;
    // Made by the first watch/set. Only its own thread touches watchBurst_.
    std::vector<FileEvent>       watchBurst_;
    std::unique_ptr<FileWatcher> watcher_;

    LifetimeGuard lifetime_;
};

} // namespace ned::editor::host

#endif // NED_EDITOR_HOST_DISPATCHER_H
