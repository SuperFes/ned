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
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

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
    // Stops running searches and waits for them.
    ~Dispatcher();

    Dispatcher(const Dispatcher&)            = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

  private:
    struct SearchJob;

    void Hello(const nlohmann::json& params, protocol::Responder responder);
    void SearchProject(const nlohmann::json& params, protocol::Responder responder);
    void FinishSearch(std::uint64_t job, std::vector<SearchMatch> matches, const std::string& error);

    protocol::Endpoint&                         endpoint_;
    PostQueue&                                  postQueue_;
    Identity                                    identity_;
    std::function<void(std::string_view codec)> onCodec_;

    std::uint64_t                                                 nextJob_ = 0;
    std::unordered_map<std::uint64_t, std::unique_ptr<SearchJob>> searches_;

    LifetimeGuard lifetime_;
};

} // namespace ned::editor::host

#endif // NED_EDITOR_HOST_DISPATCHER_H
