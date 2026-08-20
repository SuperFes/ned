#include "VcsRunner.h"

#include "Editor/ProjectRoot.h"
#include "Editor/Tasks/TaskProcess.h"
#include "Text/Buffer.h"
#include "VcsProviderRegistry.h"

namespace ned::editor::vcs {

namespace {

    // Diagnostic detail for a failed blame/log run -- distinguishes "the
    // process never started" (exitCode is nullopt with no output, e.g. the
    // executable wasn't found or ChildProcess itself threw), "killed by a
    // signal" (nullopt with some output already captured), and "exited
    // non-zero" (the process ran but git itself reported failure, most
    // often a real, informative message on its own stdout/stderr) --
    // included directly in the status message rather than a bare "git
    // blame failed" so a real failure (wrong argv, no such repo, git
    // missing) is diagnosable from the editor alone, not just a debugger.
    std::string BlameOrLogFailureDetail(const char* operation, std::optional<int> exitCode, const std::string& output) {
        std::string detail = std::string("git ") + operation + " failed";
        if (!exitCode) {
            detail += output.empty() ? " (couldn't start the process -- is git on $PATH?)" : " (terminated)";
        }
        else {
            detail += " (exit " + std::to_string(*exitCode) + ")";
        }
        if (!output.empty()) {
            constexpr std::size_t kMaxDetailLength = 200;
            std::string           trimmed          = output.substr(0, kMaxDetailLength);
            while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
                trimmed.pop_back();
            }
            detail += ": " + trimmed;
        }
        return detail;
    }

} // namespace

VcsRunner::VcsRunner(ned::ui::EventLoop& eventLoop) : eventLoop_(eventLoop) {
}

bool VcsRunner::IsRunning(const std::string& key) const {
    return running_.contains(key);
}

void VcsRunner::RunAndCollect(const std::string& key, const std::vector<std::string>& argv,
                              std::function<void(std::string, std::optional<int>)> onDone) {
    auto output = std::make_shared<std::string>();
    try {
        running_[key] = std::make_unique<tasks::TaskProcess>(
            argv, eventLoop_, [output](std::string_view chunk) { output->append(chunk); },
            [this, key, output, onDone](std::optional<int> exitCode) {
                std::string collected = std::move(*output);
                // erase() destroys the owning TaskProcess -- including this
                // very closure's own storage (onExit_) -- so it must be the
                // unconditional LAST statement here, exactly mirroring
                // TaskRunner::RunTask's own onExit lambda; calling onDone
                // afterward (the original, buggy order) reads captured
                // state (onDone/output/provider by extension) out of
                // already-freed memory -- a real, crash-confirmed bug
                // caught via a coredump: `provider` came out pointing at
                // unmapped memory and the call through it segfaulted.
                onDone(std::move(collected), exitCode);
                running_.erase(key);
            });
    }
    catch (const std::exception&) {
        onDone({}, std::nullopt); // couldn't even spawn -- e.g. the configured executable isn't on $PATH
    }
}

void VcsRunner::RequestBlame(const text::Buffer& buffer, std::function<void(std::vector<VcsBlameLine>)> onComplete,
                             std::function<void(std::string)> onError) {
    if (!buffer.Path()) {
        onError("no file associated with this buffer");
        return;
    }
    // weakly_canonical, not a bare copy of buffer.Path(): a provider's
    // BlameArgv/LogArgv may run the command via "-C <dirname(path)>" (the
    // bundled git plugin does, see vcs-git.janet), which changes the
    // subprocess's own cwd -- a *relative* path (e.g. "../ROADMAP.md", the
    // literal string ned was invoked with from a subdirectory) would then
    // get re-resolved against that *new* cwd instead of the original one,
    // landing somewhere entirely different. A real, reported bug: git
    // reported "'../ROADMAP.md' outside repository" because "-C .." plus
    // the still-relative "../ROADMAP.md" compounds to one directory too
    // far up. An absolute, ".."-resolved path has no such ambiguity
    // regardless of what cwd a provider's own command ends up running
    // from. Mirrors BufferView::HandleRenameFileKey's own use of
    // weakly_canonical for the same "make this path trustworthy regardless
    // of how it was originally typed" reason.
    const std::filesystem::path path = std::filesystem::weakly_canonical(*buffer.Path());

    VcsProvider* provider = ActiveProviderFor(ProjectRoot());
    if (!provider) {
        onError("no vcs provider registered for this project");
        return;
    }

    const std::string key = "blame:" + path.string();
    if (IsRunning(key)) {
        onError("blame is already running for this file");
        return;
    }

    VcsCommandSpec spec;
    try {
        spec = provider->BlameArgv(path);
    }
    catch (const std::exception& e) {
        onError(e.what());
        return;
    }

    RunAndCollect(key, spec.argv, [provider, onComplete, onError](std::string output, std::optional<int> exitCode) {
        if (!exitCode || *exitCode != 0) {
            onError(BlameOrLogFailureDetail("blame", exitCode, output));
            return;
        }
        try {
            onComplete(provider->ParseBlame(output));
        }
        catch (const std::exception& e) {
            onError(e.what());
        }
    });
}

void VcsRunner::RequestLog(const text::Buffer& buffer, std::function<void(std::vector<VcsLogEntry>)> onComplete,
                           std::function<void(std::string)> onError) {
    if (!buffer.Path()) {
        onError("no file associated with this buffer");
        return;
    }
    // weakly_canonical, not a bare copy of buffer.Path(): a provider's
    // BlameArgv/LogArgv may run the command via "-C <dirname(path)>" (the
    // bundled git plugin does, see vcs-git.janet), which changes the
    // subprocess's own cwd -- a *relative* path (e.g. "../ROADMAP.md", the
    // literal string ned was invoked with from a subdirectory) would then
    // get re-resolved against that *new* cwd instead of the original one,
    // landing somewhere entirely different. A real, reported bug: git
    // reported "'../ROADMAP.md' outside repository" because "-C .." plus
    // the still-relative "../ROADMAP.md" compounds to one directory too
    // far up. An absolute, ".."-resolved path has no such ambiguity
    // regardless of what cwd a provider's own command ends up running
    // from. Mirrors BufferView::HandleRenameFileKey's own use of
    // weakly_canonical for the same "make this path trustworthy regardless
    // of how it was originally typed" reason.
    const std::filesystem::path path = std::filesystem::weakly_canonical(*buffer.Path());

    VcsProvider* provider = ActiveProviderFor(ProjectRoot());
    if (!provider) {
        onError("no vcs provider registered for this project");
        return;
    }

    const std::string key = "log:" + path.string();
    if (IsRunning(key)) {
        onError("log is already running for this file");
        return;
    }

    VcsCommandSpec spec;
    try {
        spec = provider->LogArgv(path);
    }
    catch (const std::exception& e) {
        onError(e.what());
        return;
    }

    RunAndCollect(key, spec.argv, [provider, onComplete, onError](std::string output, std::optional<int> exitCode) {
        if (!exitCode || *exitCode != 0) {
            onError(BlameOrLogFailureDetail("log", exitCode, output));
            return;
        }
        try {
            onComplete(provider->ParseLog(output));
        }
        catch (const std::exception& e) {
            onError(e.what());
        }
    });
}

} // namespace ned::editor::vcs
