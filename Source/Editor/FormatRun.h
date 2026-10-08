//
// The format command (FormatOnSave.h) run beside a buffer, without waiting
// for it: on a remote buffer's host, in the project root found there
// (Remote/Placement.h), which the main thread can't block on. One run at a
// time; starting another, or destroying this, abandons the one before.
//

#ifndef NED_EDITOR_FORMATRUN_H
#define NED_EDITOR_FORMATRUN_H

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "Editor/Lifetime.h"
#include "Editor/PostQueue.h"
#include "Editor/Process/StreamedChild.h"
#include "Text/RemoteFile.h"

namespace ned::editor {

namespace remote {
    class Hosts;
} // namespace remote

class FormatRun {
  public:
    // The formatted text, or why there is none.
    using Done = std::function<void(std::expected<std::string, std::string> formatted)>;

    // postQueue must outlive this object.
    explicit FormatRun(PostQueue& postQueue);
    ~FormatRun();

    FormatRun(const FormatRun&)            = delete;
    FormatRun& operator=(const FormatRun&) = delete;

    // Main thread only. done runs there, never before this returns, and not
    // at all once the run is abandoned. hosts may be null for a local one.
    void               Start(remote::Hosts* hosts, const std::optional<text::RemoteFile>& beside, const std::string& command, std::string text, Done done);
    void               Cancel();
    [[nodiscard]] bool Running() const noexcept;

  private:
    void Started(std::expected<process::ChildProcess, std::string> child, std::string text, Done done);
    void Finish(std::expected<std::string, std::string> formatted, Done done);

    PostQueue&                              postQueue_;
    bool                                    running_ = false;
    std::unique_ptr<process::StreamedChild> process_;
    // Revoked when a run is abandoned, so nothing of it arrives after.
    LifetimeGuard run_;
};

} // namespace ned::editor

#endif // NED_EDITOR_FORMATRUN_H
