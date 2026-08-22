#include "VcsRunner.h"

#include <cstdlib>
#include <vector>

#include <unistd.h>

#include "DiffPatch.h"
#include "Editor/ProjectRoot.h"
#include "Editor/Tasks/TaskProcess.h"
#include "Text/Buffer.h"
#include "VcsProviderRegistry.h"

namespace ned::editor::vcs {

std::filesystem::path VcsCommitMessagePath() {
    return std::filesystem::temp_directory_path() / std::string(kVcsCommitMessageFilename);
}

std::string ExtractCommitMessage(std::string_view bufferText) {
    std::string result;
    std::size_t lineStart = 0;
    while (lineStart <= bufferText.size()) {
        const std::size_t lineEnd = bufferText.find('\n', lineStart);
        const std::size_t lineStop = lineEnd == std::string_view::npos ? bufferText.size() : lineEnd;
        const std::string_view line = bufferText.substr(lineStart, lineStop - lineStart);
        if (!line.starts_with('#')) {
            result.append(line);
            result.push_back('\n');
        }
        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }
    while (!result.empty() && (result.back() == '\n' || result.back() == ' ' || result.back() == '\t' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

namespace {

    // Diagnostic detail for a failed run -- distinguishes "the process
    // never started" (exitCode is nullopt with no output, e.g. the
    // executable wasn't found or ChildProcess itself threw), "killed by a
    // signal" (nullopt with some output already captured), and "exited
    // non-zero" (the process ran but the VCS itself reported failure, most
    // often a real, informative message on its own stdout/stderr) --
    // included directly in the status message rather than a bare "vcs
    // blame failed" so a real failure (wrong argv, no such repo, the VCS
    // executable missing) is diagnosable from the editor alone, not just a
    // debugger. (Was "git <operation> failed" until the vocabulary-
    // completion follow-up -- the runner never knows which VCS the active
    // provider actually shells out to, so naming git here was only ever
    // borrowed from the bundled plugin being the sole provider.)
    std::string OperationFailureDetail(const char* operation, std::optional<int> exitCode, const std::string& output) {
        std::string detail = std::string("vcs ") + operation + " failed";
        if (!exitCode) {
            detail += output.empty() ? " (couldn't start the process -- is the vcs executable on $PATH?)" : " (terminated)";
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

    // Writes content to a uniquely-named temp file via mkstemp, mirroring
    // FormatOnSave.cpp's MakeTempFile (same TOCTOU-avoidance reasoning --
    // see its own comment) plus the actual write. nullopt on any failure,
    // with the partial file removed rather than left behind.
    std::optional<std::filesystem::path> WritePatchFile(const std::string& content) {
        const std::string templatePath = (std::filesystem::temp_directory_path() / "ned-vcs-patch-XXXXXX").string();
        std::vector<char> nameBuffer(templatePath.begin(), templatePath.end());
        nameBuffer.push_back('\0');

        const int fd = ::mkstemp(nameBuffer.data());
        if (fd == -1) {
            return std::nullopt;
        }
        const std::filesystem::path path(nameBuffer.data());

        std::size_t written = 0;
        while (written < content.size()) {
            const ssize_t chunk = ::write(fd, content.data() + written, content.size() - written);
            if (chunk <= 0) {
                ::close(fd);
                std::error_code ec;
                std::filesystem::remove(path, ec);
                return std::nullopt;
            }
            written += static_cast<std::size_t>(chunk);
        }
        ::close(fd);
        return path;
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
            onError(OperationFailureDetail("blame", exitCode, output));
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
            onError(OperationFailureDetail("log", exitCode, output));
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

void VcsRunner::RequestDiff(const text::Buffer& buffer, std::function<void(std::vector<VcsDiffHunk>)> onComplete,
                            std::function<void(std::string)> onError) {
    if (!buffer.Path()) {
        onError("no file associated with this buffer");
        return;
    }
    const std::filesystem::path path = std::filesystem::weakly_canonical(*buffer.Path());

    VcsProvider* provider = ActiveProviderFor(ProjectRoot());
    if (!provider) {
        onError("no vcs provider registered for this project");
        return;
    }

    const std::string key = "diff:" + path.string();
    if (IsRunning(key)) {
        onError("diff is already running for this file");
        return;
    }

    VcsCommandSpec spec;
    try {
        spec = provider->DiffArgv(path);
    }
    catch (const std::exception& e) {
        onError(e.what());
        return;
    }

    RunAndCollect(key, spec.argv, [provider, onComplete, onError](std::string output, std::optional<int> exitCode) {
        if (!exitCode || *exitCode != 0) {
            onError(OperationFailureDetail("diff", exitCode, output));
            return;
        }
        try {
            onComplete(provider->ParseDiff(output));
        }
        catch (const std::exception& e) {
            onError(e.what());
        }
    });
}

void VcsRunner::RunProviderOperation(const char* operation, const std::string& key,
                                     const std::function<VcsCommandSpec(VcsProvider&)>& buildSpec,
                                     std::function<void(VcsProvider&, std::string)>     onOutput,
                                     std::function<void(std::string)>                   onError) {
    VcsProvider* provider = ActiveProviderFor(ProjectRoot());
    if (!provider) {
        onError("no vcs provider registered for this project");
        return;
    }

    if (IsRunning(key)) {
        onError(std::string(operation) + " is already running");
        return;
    }

    VcsCommandSpec spec;
    try {
        spec = buildSpec(*provider);
    }
    catch (const std::exception& e) {
        onError(e.what());
        return;
    }

    RunAndCollect(key, spec.argv,
                  [operation, provider, onOutput = std::move(onOutput), onError](std::string output, std::optional<int> exitCode) {
                      if (!exitCode || *exitCode != 0) {
                          onError(OperationFailureDetail(operation, exitCode, output));
                          return;
                      }
                      try {
                          onOutput(*provider, std::move(output));
                      }
                      catch (const std::exception& e) {
                          onError(e.what());
                      }
                  });
}

void VcsRunner::RequestStatus(std::function<void(std::vector<VcsStatusEntry>)> onComplete,
                              std::function<void(std::string)>                 onError) {
    const std::filesystem::path root = ProjectRoot();
    RunProviderOperation(
        "status", "status:" + root.string(),
        [&root](VcsProvider& provider) { return provider.StatusArgv(root); },
        [onComplete = std::move(onComplete)](VcsProvider& provider, std::string output) {
            onComplete(provider.ParseStatus(output));
        },
        std::move(onError));
}

void VcsRunner::RequestStage(const std::filesystem::path& path, std::function<void()> onSuccess,
                             std::function<void(std::string)> onError) {
    // weakly_canonical for the same relative-path-vs-"-C" reason
    // RequestBlame spells out above.
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    RunProviderOperation(
        "stage", "stage:" + canonical.string(),
        [&canonical](VcsProvider& provider) { return provider.StageArgv(canonical); },
        [onSuccess = std::move(onSuccess)](VcsProvider&, std::string) { onSuccess(); }, std::move(onError));
}

void VcsRunner::RequestUnstage(const std::filesystem::path& path, std::function<void()> onSuccess,
                               std::function<void(std::string)> onError) {
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
    RunProviderOperation(
        "unstage", "unstage:" + canonical.string(),
        [&canonical](VcsProvider& provider) { return provider.UnstageArgv(canonical); },
        [onSuccess = std::move(onSuccess)](VcsProvider&, std::string) { onSuccess(); }, std::move(onError));
}

void VcsRunner::RequestHunkApply(const text::Buffer& buffer, std::size_t targetLine, bool stage,
                                 std::function<void()> onSuccess, std::function<void(std::string)> onError) {
    if (!buffer.Path()) {
        onError("no file associated with this buffer");
        return;
    }
    // weakly_canonical for the same relative-path-vs-"-C" reason
    // RequestBlame spells out above.
    const std::filesystem::path path      = std::filesystem::weakly_canonical(*buffer.Path());
    const char*                 operation = stage ? "stage hunk" : "unstage hunk";
    const std::string           keyPrefix = stage ? "stage-hunk" : "unstage-hunk";

    RunProviderOperation(
        operation, keyPrefix + "-diff:" + path.string(),
        [&path, stage](VcsProvider& provider) { return stage ? provider.DiffArgv(path) : provider.StagedDiffArgv(path); },
        [this, path, targetLine, stage, operation, keyPrefix, onSuccess = std::move(onSuccess),
         onError](VcsProvider&, std::string diffOutput) {
            const std::optional<std::string> patch = ExtractHunkPatch(diffOutput, targetLine);
            if (!patch) {
                onError(stage ? "no unstaged change at this line" : "no staged change at this line");
                return;
            }
            const std::optional<std::filesystem::path> patchFile = WritePatchFile(*patch);
            if (!patchFile) {
                onError("couldn't write the patch to a temp file");
                return;
            }
            auto removePatchFile = [patchFile] {
                std::error_code ec;
                std::filesystem::remove(*patchFile, ec); // best-effort -- a leftover temp file isn't worth an error
            };
            const std::filesystem::path root = ProjectRoot();
            RunProviderOperation(
                operation, keyPrefix + "-apply:" + path.string(),
                [&root, &patchFile, stage](VcsProvider& provider) {
                    return stage ? provider.StagePatchArgv(root, *patchFile) : provider.UnstagePatchArgv(root, *patchFile);
                },
                [removePatchFile, onSuccess](VcsProvider&, std::string) {
                    removePatchFile();
                    onSuccess();
                },
                [removePatchFile, onError](std::string error) {
                    removePatchFile();
                    onError(error);
                });
        },
        onError);
}

void VcsRunner::RequestCommit(const std::string& message, std::function<void(std::string)> onSuccess,
                              std::function<void(std::string)> onError) {
    const std::filesystem::path root = ProjectRoot();
    RunProviderOperation(
        "commit", "commit:" + root.string(),
        [&root, &message](VcsProvider& provider) { return provider.CommitArgv(root, message); },
        [onSuccess = std::move(onSuccess)](VcsProvider&, std::string output) {
            // The first output line is the VCS's own one-line summary of
            // what got committed (e.g. git's "[main abc1234] message") --
            // exactly status-line-sized, so pass it through verbatim.
            onSuccess(output.substr(0, output.find('\n')));
        },
        std::move(onError));
}

void VcsRunner::RequestBranchList(std::function<void(std::vector<VcsBranchEntry>)> onComplete,
                                  std::function<void(std::string)>                 onError) {
    const std::filesystem::path root = ProjectRoot();
    RunProviderOperation(
        "branch listing", "branch-list:" + root.string(),
        [&root](VcsProvider& provider) { return provider.BranchListArgv(root); },
        [onComplete = std::move(onComplete)](VcsProvider& provider, std::string output) {
            onComplete(provider.ParseBranchList(output));
        },
        std::move(onError));
}

void VcsRunner::RequestBranchSwitch(const std::string& name, std::function<void()> onSuccess,
                                    std::function<void(std::string)> onError) {
    const std::filesystem::path root = ProjectRoot();
    RunProviderOperation(
        "branch switch", "branch-switch:" + root.string(),
        [&root, &name](VcsProvider& provider) { return provider.BranchSwitchArgv(root, name); },
        [onSuccess = std::move(onSuccess)](VcsProvider&, std::string) { onSuccess(); }, std::move(onError));
}

void VcsRunner::RequestBranchCreate(const std::string& name, std::function<void()> onSuccess,
                                    std::function<void(std::string)> onError) {
    const std::filesystem::path root = ProjectRoot();
    RunProviderOperation(
        "branch creation", "branch-create:" + root.string(),
        [&root, &name](VcsProvider& provider) { return provider.BranchCreateArgv(root, name); },
        [onSuccess = std::move(onSuccess)](VcsProvider&, std::string) { onSuccess(); }, std::move(onError));
}

} // namespace ned::editor::vcs
