#include "FormatRun.h"

#include <utility>

#include "Editor/FormatOnSave.h"
#include "Editor/ProcessTimeouts.h"
#include "Editor/Remote/Placement.h"

namespace ned::editor {

FormatRun::FormatRun(PostQueue& postQueue) : postQueue_(postQueue) {
}

FormatRun::~FormatRun() {
    Cancel();
}

void FormatRun::Start(remote::Hosts* hosts, const std::optional<text::RemoteFile>& beside, const std::string& command, std::string text, Done done) {
    Cancel();
    running_ = true;
    remote::SpawnBeside(hosts, beside,
                        remote::RemoteSpawn{.argv = FormatCommandArgv(command), .cwd = std::nullopt, .env = {}, .stderrMode = process::StderrMode::Discard},
                        run_.Token(), [this, text = std::move(text), done = std::move(done)](remote::Spawned spawned) mutable {
                            std::expected<process::ChildProcess, std::string> child =
                                spawned ? std::expected<process::ChildProcess, std::string>(std::move(spawned->child)) : std::unexpected(std::move(spawned.error()));
                            // A local spawn answers before SpawnBeside returns.
                            postQueue_.Post(run_.Bind([this, child = std::make_shared<decltype(child)>(std::move(child)), text = std::move(text),
                                                       done = std::move(done)]() mutable { Started(std::move(*child), std::move(text), std::move(done)); }));
                        });
}

void FormatRun::Cancel() {
    run_.Revoke();
    process_.reset();
    running_ = false;
}

bool FormatRun::Running() const noexcept {
    return running_;
}

void FormatRun::Started(std::expected<process::ChildProcess, std::string> child, std::string text, Done done) {
    if (!child) {
        Finish(std::unexpected(std::move(child.error())), std::move(done));
        return;
    }
    auto output = std::make_shared<std::string>();
    process_    = std::make_unique<process::StreamedChild>(
        std::move(*child), postQueue_, [output](std::string_view chunk) { output->append(chunk); },
        [this, output, done = std::move(done)](std::optional<int> exitCode) {
            std::optional<std::string> formatted = AcceptFormatOutput(std::move(*output), exitCode);
            // Posted, so done may start another run, which destroys the
            // child whose handler this is.
            postQueue_.Post(run_.Bind([this, formatted = std::move(formatted), done]() mutable {
                Finish(formatted ? std::expected<std::string, std::string>(std::move(*formatted)) : std::unexpected(std::string("it failed or printed nothing")), done);
            }));
        });
    process_->Feed(std::move(text), SubprocessWriteTimeoutMs());
}

void FormatRun::Finish(std::expected<std::string, std::string> formatted, Done done) {
    running_ = false;
    process_.reset();
    done(std::move(formatted));
}

} // namespace ned::editor
