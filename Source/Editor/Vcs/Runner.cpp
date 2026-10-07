#include "Runner.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <vector>

#include <unistd.h>

#include "CurrentBranch.h"
#include "DiffPatch.h"
#include "Editor/DiagnosticsLog.h"
#include "Editor/ProcessTimeouts.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/Tasks/TaskProcess.h"
#include "HostRepo.h"
#include "ProviderRegistry.h"
#include "Text/Buffer.h"

namespace ned::editor::vcs {

std::filesystem::path CommitMessagePath() {
    return std::filesystem::temp_directory_path() / std::string(kVcsCommitMessageFilename);
}

std::string ExtractCommitMessage(std::string_view bufferText) {
    std::string result;
    std::size_t lineStart = 0;
    while (lineStart <= bufferText.size()) {
        const std::size_t      lineEnd  = bufferText.find('\n', lineStart);
        const std::size_t      lineStop = lineEnd == std::string_view::npos ? bufferText.size() : lineEnd;
        const std::string_view line     = bufferText.substr(lineStart, lineStop - lineStart);
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

    bool IsCommitMessage(const std::filesystem::path& path) {
        try {
            return path.lexically_normal() == CommitMessagePath().lexically_normal();
        }
        catch (const std::filesystem::filesystem_error&) {
            return false;
        }
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

Runner::Runner(ned::ui::EventLoop& eventLoop) : eventLoop_(eventLoop) {
}

void Runner::SetHosts(remote::Hosts* hosts) {
    hosts_ = hosts;
}

bool Runner::IsRunning(const std::string& key) const {
    return running_.contains(key);
}

std::filesystem::path Runner::Canonical(const Repo& repo, const std::filesystem::path& path) {
    // A path as typed can be relative to ned's cwd, which a provider's "-C
    // <dirname>" would re-resolve against the wrong directory. A host's
    // paths are absolute already, and this machine can't resolve them.
    return repo.IsRemote() ? path.lexically_normal() : std::filesystem::weakly_canonical(path);
}

void Runner::WithProvider(const Repo& repo, std::function<void(std::string)> onError, std::function<void(Provider& provider)> run) {
    constexpr const char* kNoProvider = "no vcs provider registered for this project";
    if (!repo.IsRemote()) {
        if (Provider* provider = ActiveProviderFor(repo.root)) {
            run(*provider);
        }
        else {
            onError(kNoProvider);
        }
        return;
    }
    if (hosts_ == nullptr) {
        onError("no remote host can reach " + repo.ToString());
        return;
    }
    FindProviderOnHost(*hosts_, repo, lifetime_.Token(),
                       [onError = std::move(onError), run = std::move(run)](std::expected<Provider*, std::string> provider) {
                           if (!provider) {
                               onError(provider.error());
                           }
                           else if (*provider == nullptr) {
                               onError(kNoProvider);
                           }
                           else {
                               run(**provider);
                           }
                       });
}

void Runner::InBufferRepo(const text::Buffer& buffer, std::function<void(std::string)> onError,
                          std::function<void(const Repo& repo, const std::filesystem::path& path)> run) {
    if (buffer.Path()) {
        const Repo repo = LocalRepo();
        run(repo, Canonical(repo, *buffer.Path()));
        return;
    }
    if (!buffer.Remote()) {
        onError("no file associated with this buffer");
        return;
    }
    RepoFor(buffer, [path = buffer.Remote()->path, onError, run = std::move(run)](std::expected<Repo, std::string> repo) {
        if (!repo) {
            onError(repo.error());
            return;
        }
        run(*repo, path);
    });
}

void Runner::FindRepo(const text::RemoteFile& file, std::function<void(std::expected<Repo, std::string>)> done) {
    if (hosts_ == nullptr) {
        done(std::unexpected("no remote host can reach " + file.ToString()));
        return;
    }
    FindRepoOnHost(*hosts_, file, lifetime_.Token(), std::move(done));
}

void Runner::RepoFor(const text::Buffer& buffer, std::function<void(std::expected<Repo, std::string>)> done) {
    if (buffer.Path() && !IsCommitMessage(*buffer.Path())) {
        lastRemote_.reset();
        done(LocalRepo());
        return;
    }
    if (!buffer.Remote()) {
        done(lastRemote_ ? *lastRemote_ : LocalRepo());
        return;
    }
    std::string key = buffer.Remote()->ToString();
    if (const auto found = remoteRepos_.find(key); found != remoteRepos_.end()) {
        lastRemote_ = found->second;
        done(found->second);
        return;
    }
    FindRepo(*buffer.Remote(), [this, key = std::move(key), done = std::move(done)](std::expected<Repo, std::string> repo) {
        if (repo) {
            remoteRepos_.insert_or_assign(key, *repo);
            lastRemote_ = *repo;
        }
        done(std::move(repo));
    });
}

void Runner::RunAndCollect(const Repo& repo, const std::string& key, CommandSpec spec,
                           std::function<void(std::string, std::optional<int>)> onDone) {
    Run& run     = running_[key];
    run.starting = std::make_unique<LifetimeGuard>();
    remote::SpawnBeside(hosts_, repo.Beside(),
                        remote::RemoteSpawn{.argv       = std::move(spec.argv),
                                            .cwd        = repo.IsRemote() ? std::optional(repo.root) : std::nullopt,
                                            .env        = {},
                                            .stderrMode = process::StderrMode::MergeWithStdout},
                        run.starting->Token(),
                        [this, key, input = std::move(spec.input), onDone = std::move(onDone)](remote::Spawned spawned) mutable {
                            Started(key, std::move(spawned), std::move(input), std::move(onDone));
                        });
}

void Runner::Started(const std::string& key, remote::Spawned spawned, std::optional<std::string> input,
                     std::function<void(std::string, std::optional<int>)> onDone) {
    const auto failed = [this, &key, &onDone](const std::string& why) {
        // onDone's (string, optional<int>) has nowhere to carry why, so the
        // log is the one place it shows.
        LogMessage(LogCategory::Vcs, LogSeverity::Error, "failed to run VCS command: " + why);
        running_.erase(key);
        onDone({}, std::nullopt);
    };
    if (!spawned) {
        failed(spawned.error());
        return;
    }
    try {
        if (input) {
            spawned->child.WriteAll(*input, SubprocessWriteTimeoutMs());
            spawned->child.CloseInput();
        }
    }
    catch (const std::exception& e) {
        failed(e.what());
        return;
    }
    auto output = std::make_shared<std::string>();
    Run& run    = running_[key];
    run.starting.reset();
    run.process = std::make_unique<tasks::TaskProcess>(
        std::move(spawned->child), eventLoop_, [output](std::string_view chunk) { output->append(chunk); },
        [this, key, output, onDone = std::move(onDone)](std::optional<int> exitCode) {
            std::string collected = std::move(*output);
            // erase() destroys the TaskProcess that owns this closure, so it
            // must come last: anything after it reads freed captures.
            onDone(std::move(collected), exitCode);
            running_.erase(key);
        });
}

void Runner::RequestBlame(const text::Buffer& buffer, std::function<void(std::vector<BlameLine>)> onComplete,
                          std::function<void(std::string)> onError) {
    InBufferRepo(buffer, onError, [this, onComplete, onError](const Repo& repo, const std::filesystem::path& path) {
        RunProviderOperation(
            repo, "blame", "blame:" + path.string(), [path](Provider& provider) { return provider.BlameArgv(path); },
            [onComplete](Provider& provider, std::string output) { onComplete(provider.ParseBlame(output)); }, onError);
    });
}

void Runner::RequestLog(const text::Buffer& buffer, std::function<void(std::vector<LogEntry>)> onComplete,
                        std::function<void(std::string)> onError) {
    InBufferRepo(buffer, onError, [this, onComplete, onError](const Repo& repo, const std::filesystem::path& path) {
        RunProviderOperation(
            repo, "log", "log:" + path.string(), [path](Provider& provider) { return provider.LogArgv(path); },
            [onComplete](Provider& provider, std::string output) { onComplete(provider.ParseLog(output)); }, onError);
    });
}

void Runner::RequestDiff(const text::Buffer& buffer, std::function<void(std::vector<DiffHunk>)> onComplete,
                         std::function<void(std::string)> onError) {
    InBufferRepo(buffer, onError, [this, onComplete, onError](const Repo& repo, const std::filesystem::path& path) {
        RunProviderOperation(
            repo, "diff", "diff:" + path.string(), [path](Provider& provider) { return provider.DiffArgv(path); },
            [onComplete](Provider& provider, std::string output) { onComplete(provider.ParseDiff(output)); }, onError);
    });
}

void Runner::RequestFullDiff(const Repo& repo, std::function<void(std::string)> onComplete, std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "full diff", "full-diff:" + root.string(),
        [root](Provider& provider) { return provider.WorkingDiffArgv(root); },
        [onComplete = std::move(onComplete)](Provider&, std::string output) { onComplete(std::move(output)); },
        std::move(onError));
}

void Runner::RequestCommitDiff(const Repo& repo, const std::string& commitHash, std::function<void(std::string)> onComplete,
                               std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "commit diff", "commit-diff:" + root.string() + ":" + commitHash,
        [root, commitHash](Provider& provider) { return provider.CommitDiffArgv(root, commitHash); },
        [onComplete = std::move(onComplete)](Provider&, std::string output) { onComplete(std::move(output)); },
        std::move(onError));
}

void Runner::RunProviderOperation(const Repo& repo, const char* operation, const std::string& key,
                                  std::function<CommandSpec(Provider&)>       buildSpec,
                                  std::function<void(Provider&, std::string)> onOutput,
                                  std::function<void(std::string)>            onError) {
    // A host's paths can match this machine's, so its runs are keyed apart.
    std::string scoped = repo.destination ? *repo.destination + ":" + key : key;
    WithProvider(repo, onError,
                 [this, repo, operation, scoped = std::move(scoped), buildSpec = std::move(buildSpec), onOutput = std::move(onOutput),
                  onError](Provider& found) mutable {
                     Provider* provider = &found;
                     if (IsRunning(scoped)) {
                         onError(std::string(operation) + " is already running");
                         return;
                     }
                     CommandSpec spec;
                     try {
                         spec = buildSpec(*provider);
                     }
                     catch (const std::exception& e) {
                         onError(e.what());
                         return;
                     }
                     RunAndCollect(repo, scoped, std::move(spec),
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
                 });
}

void Runner::ApplyPatch(const Repo& repo, const char* operation, const std::string& key, std::string patch,
                        std::function<CommandSpec(Provider&, const std::filesystem::path& patchPath)> argv,
                        std::function<void()> onSuccess, std::function<void(std::string)> onError) {
    auto patchFile       = std::make_shared<std::optional<std::filesystem::path>>();
    auto removePatchFile = [patchFile] {
        if (*patchFile) {
            std::error_code ec;
            std::filesystem::remove(**patchFile, ec); // best-effort -- a leftover temp file isn't worth an error
        }
    };
    RunProviderOperation(
        repo, operation, key,
        [onHost = repo.IsRemote(), patch = std::move(patch), argv = std::move(argv), patchFile](Provider& provider) {
            if (provider.PatchesOnStdin()) {
                CommandSpec spec = argv(provider, "-");
                spec.input       = patch;
                return spec;
            }
            if (onHost) {
                throw std::runtime_error("this provider can only apply a patch from a file on this machine");
            }
            *patchFile = WritePatchFile(patch);
            if (!*patchFile) {
                throw std::runtime_error("couldn't write the patch to a temp file");
            }
            return argv(provider, **patchFile);
        },
        [removePatchFile, onSuccess = std::move(onSuccess)](Provider&, std::string) {
            removePatchFile();
            onSuccess();
        },
        [removePatchFile, onError = std::move(onError)](std::string error) {
            removePatchFile();
            onError(std::move(error));
        });
}

void Runner::RequestStatus(const Repo& repo, std::function<void(std::vector<StatusEntry>)> onComplete,
                           std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "status", "status:" + root.string(),
        [root](Provider& provider) { return provider.StatusArgv(root); },
        [onComplete = std::move(onComplete)](Provider& provider, std::string output) {
            onComplete(provider.ParseStatus(output));
        },
        std::move(onError));
}

void Runner::RequestStage(const Repo& repo, const std::filesystem::path& path, std::function<void()> onSuccess,
                          std::function<void(std::string)> onError) {
    const std::filesystem::path canonical = Canonical(repo, path);
    RunProviderOperation(
        repo, "stage", "stage:" + canonical.string(),
        [canonical](Provider& provider) { return provider.StageArgv(canonical); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string) { onSuccess(); }, std::move(onError));
}

void Runner::RequestUnstage(const Repo& repo, const std::filesystem::path& path, std::function<void()> onSuccess,
                            std::function<void(std::string)> onError) {
    const std::filesystem::path canonical = Canonical(repo, path);
    RunProviderOperation(
        repo, "unstage", "unstage:" + canonical.string(),
        [canonical](Provider& provider) { return provider.UnstageArgv(canonical); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string) { onSuccess(); }, std::move(onError));
}

void Runner::RequestHunkApply(const text::Buffer& buffer, std::size_t targetLine, bool stage,
                              std::function<void()> onSuccess, std::function<void(std::string)> onError) {
    InBufferRepo(buffer, onError, [this, targetLine, stage, onSuccess, onError](const Repo& repo, const std::filesystem::path& path) {
        if (stage) {
            RequestHunkApplyForPath(repo, path, targetLine, stage, onSuccess, onError);
            return;
        }
        // Point counts worktree lines but the staged diff counts index
        // lines; unstaged edits above the hunk shift one against the other,
        // so translate through the unstaged diff first.
        RunProviderOperation(
            repo, "unstage hunk", "unstage-hunk-map:" + path.string(),
            [path](Provider& provider) {
                (void)provider.StagedDiffArgv(path); // fail before spawning anything if the chain can't finish
                return provider.DiffArgv(path);
            },
            [this, repo, path, targetLine, onSuccess, onError](Provider&, std::string diffOutput) {
                RequestHunkApplyForPath(repo, path, MapNewLineToOldLine(diffOutput, targetLine), /*stage=*/false, onSuccess, onError);
            },
            onError);
    });
}

void Runner::RequestHunkApply(const Repo& repo, const std::filesystem::path& path, std::size_t targetLine, bool stage,
                              std::function<void()> onSuccess, std::function<void(std::string)> onError) {
    RequestHunkApplyForPath(repo, Canonical(repo, path), targetLine, stage, std::move(onSuccess), std::move(onError));
}

void Runner::RequestHunkApplyForPath(const Repo& repo, const std::filesystem::path& path, std::size_t targetLine, bool stage,
                                     std::function<void()> onSuccess, std::function<void(std::string)> onError) {
    const char*       operation = stage ? "stage hunk" : "unstage hunk";
    const std::string keyPrefix = stage ? "stage-hunk" : "unstage-hunk";

    RunProviderOperation(
        repo, operation, keyPrefix + "-diff:" + path.string(),
        [path, stage](Provider& provider) { return stage ? provider.DiffArgv(path) : provider.StagedDiffArgv(path); },
        [this, repo, path, targetLine, stage, operation, keyPrefix, onSuccess = std::move(onSuccess), onError](Provider&, std::string diffOutput) {
            std::optional<std::string> patch = ExtractHunkPatch(diffOutput, targetLine);
            if (!patch) {
                onError(stage ? "no unstaged change at this line" : "no staged change at this line");
                return;
            }
            ApplyPatch(
                repo, operation, keyPrefix + "-apply:" + path.string(), std::move(*patch),
                [root = repo.root, stage](Provider& provider, const std::filesystem::path& patchPath) {
                    return stage ? provider.StagePatchArgv(root, patchPath) : provider.UnstagePatchArgv(root, patchPath);
                },
                onSuccess, onError);
        },
        onError);
}

void Runner::RequestHunkRevert(const text::Buffer& buffer, std::size_t targetLine, std::function<void()> onSuccess,
                               std::function<void(std::string)> onError) {
    InBufferRepo(buffer, onError, [this, targetLine, onSuccess, onError](const Repo& repo, const std::filesystem::path& path) {
        RunProviderOperation(
            repo, "revert hunk", "revert-hunk-diff:" + path.string(), [path](Provider& provider) { return provider.DiffArgv(path); },
            [this, repo, path, targetLine, onSuccess, onError](Provider&, std::string diffOutput) {
                std::optional<std::string> patch = ExtractHunkPatch(diffOutput, targetLine);
                if (!patch) {
                    onError("no unstaged change at this line");
                    return;
                }
                ApplyPatch(
                    repo, "revert hunk", "revert-hunk-apply:" + path.string(), std::move(*patch),
                    [root = repo.root](Provider& provider, const std::filesystem::path& patchPath) { return provider.RevertPatchArgv(root, patchPath); },
                    onSuccess, onError);
            },
            onError);
    });
}

void Runner::RequestCommit(const Repo& repo, const std::string& message, std::function<void(std::string)> onSuccess,
                           std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "commit", "commit:" + root.string(),
        [root, message](Provider& provider) { return provider.CommitArgv(root, message); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string output) {
            // The first output line is the VCS's own one-line summary of
            // what got committed (e.g. git's "[main abc1234] message") --
            // exactly status-line-sized, so pass it through verbatim.
            onSuccess(output.substr(0, output.find('\n')));
        },
        std::move(onError));
}

void Runner::RequestAmendCommit(const Repo& repo, const std::string& message, std::function<void(std::string)> onSuccess,
                                std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "amend commit", "commit:" + root.string(),
        [root, message](Provider& provider) { return provider.AmendCommitArgv(root, message); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string output) {
            onSuccess(output.substr(0, output.find('\n')));
        },
        std::move(onError));
}

void Runner::RequestPreviousCommitMessage(const Repo& repo, std::function<void(std::string)> onComplete,
                                          std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "previous commit message", "previous-commit-message:" + root.string(),
        [root](Provider& provider) { return provider.PreviousCommitMessageArgv(root); },
        [onComplete = std::move(onComplete)](Provider&, std::string output) { onComplete(std::move(output)); },
        std::move(onError));
}

void Runner::RequestExtendCommit(const Repo& repo, std::function<void(std::string)> onSuccess, std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "extend commit", "commit:" + root.string(),
        [root](Provider& provider) { return provider.ExtendCommitArgv(root); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string output) {
            onSuccess(output.substr(0, output.find('\n')));
        },
        std::move(onError));
}

void Runner::RequestRewordCommit(const Repo& repo, const std::string& message, std::function<void(std::string)> onSuccess,
                                 std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "reword commit", "commit:" + root.string(),
        [root, message](Provider& provider) { return provider.RewordCommitArgv(root, message); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string output) {
            onSuccess(output.substr(0, output.find('\n')));
        },
        std::move(onError));
}

void Runner::RequestBranchList(const Repo& repo, std::function<void(std::vector<BranchEntry>)> onComplete,
                               std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "branch listing", "branch-list:" + root.string(),
        [root](Provider& provider) { return provider.BranchListArgv(root); },
        [repo, onComplete = std::move(onComplete)](Provider& provider, std::string output) {
            std::vector<BranchEntry> branches = provider.ParseBranchList(output);
            const auto               current  = std::ranges::find(branches, true, &BranchEntry::current);
            SetCurrentBranch(repo, current != branches.end() ? std::optional(current->name) : std::nullopt);
            onComplete(std::move(branches));
        },
        std::move(onError));
}

void Runner::RefreshCurrentBranch(const Repo& repo, std::function<void()> onDone) {
    RequestBranchList(repo, [onDone](const std::vector<BranchEntry>&) {
        if (onDone) {
            onDone();
        }
    });
}

void Runner::RequestBranchSwitch(const Repo& repo, const std::string& name, std::function<void()> onSuccess,
                                 std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "branch switch", "branch-switch:" + root.string(),
        [root, name](Provider& provider) { return provider.BranchSwitchArgv(root, name); },
        [repo, name, onSuccess = std::move(onSuccess)](Provider&, std::string) {
            SetCurrentBranch(repo, name);
            onSuccess();
        },
        std::move(onError));
}

void Runner::RequestBranchCreate(const Repo& repo, const std::string& name, std::function<void()> onSuccess,
                                 std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "branch creation", "branch-create:" + root.string(),
        [root, name](Provider& provider) { return provider.BranchCreateArgv(root, name); },
        [repo, name, onSuccess = std::move(onSuccess)](Provider&, std::string) {
            SetCurrentBranch(repo, name);
            onSuccess();
        },
        std::move(onError));
}

void Runner::RequestRevert(const Repo& repo, const std::filesystem::path& path, std::function<void()> onSuccess,
                           std::function<void(std::string)> onError) {
    const std::filesystem::path canonical = Canonical(repo, path);
    RunProviderOperation(
        repo, "revert", "revert:" + canonical.string(),
        [canonical](Provider& provider) { return provider.RevertArgv(canonical); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string) { onSuccess(); }, std::move(onError));
}

void Runner::RequestStashList(const Repo& repo, std::function<void(std::vector<StashEntry>)> onComplete,
                              std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "stash listing", "stash-list:" + root.string(),
        [root](Provider& provider) { return provider.StashListArgv(root); },
        [onComplete = std::move(onComplete)](Provider& provider, std::string output) {
            onComplete(provider.ParseStashList(output));
        },
        std::move(onError));
}

void Runner::RequestStashPush(const Repo& repo, const std::string& message, std::function<void()> onSuccess,
                              std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "stash push", "stash-push:" + root.string(),
        [root, message](Provider& provider) { return provider.StashPushArgv(root, message); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string) { onSuccess(); }, std::move(onError));
}

void Runner::RequestStashPop(const Repo& repo, const std::string& stashRef, std::function<void()> onSuccess,
                             std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "stash pop", "stash-pop:" + root.string() + ":" + stashRef,
        [root, stashRef](Provider& provider) { return provider.StashPopArgv(root, stashRef); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string) { onSuccess(); }, std::move(onError));
}

void Runner::RequestStashDrop(const Repo& repo, const std::string& stashRef, std::function<void()> onSuccess,
                              std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "stash drop", "stash-drop:" + root.string() + ":" + stashRef,
        [root, stashRef](Provider& provider) { return provider.StashDropArgv(root, stashRef); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string) { onSuccess(); }, std::move(onError));
}

void Runner::RequestPush(const Repo& repo, std::function<void()> onSuccess, std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "push", "push:" + root.string(), [root](Provider& provider) { return provider.PushArgv(root); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string) { onSuccess(); }, std::move(onError));
}

void Runner::RequestPull(const Repo& repo, std::function<void()> onSuccess, std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "pull", "pull:" + root.string(), [root](Provider& provider) { return provider.PullArgv(root); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string) { onSuccess(); }, std::move(onError));
}

void Runner::RequestFetch(const Repo& repo, std::function<void()> onSuccess, std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "fetch", "fetch:" + root.string(), [root](Provider& provider) { return provider.FetchArgv(root); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string) { onSuccess(); }, std::move(onError));
}

void Runner::RequestAheadBehind(const Repo& repo, std::function<void(AheadBehind)> onComplete,
                                std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "ahead/behind", "ahead-behind:" + root.string(),
        [root](Provider& provider) { return provider.AheadBehindArgv(root); },
        [onComplete = std::move(onComplete)](Provider& provider, std::string output) {
            onComplete(provider.ParseAheadBehind(output));
        },
        std::move(onError));
}

void Runner::RequestSequenceState(const Repo& repo, std::function<void(SequenceState)> onComplete,
                                  std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "sequence state", "sequence-state:" + root.string(),
        [root](Provider& provider) { return provider.SequenceStateArgv(root); },
        [onComplete = std::move(onComplete)](Provider& provider, std::string output) {
            onComplete(provider.ParseSequenceState(output));
        },
        std::move(onError));
}

void Runner::RequestSequenceContinue(const Repo& repo, const std::string& kind, std::function<void()> onSuccess,
                                     std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "continue", "sequence:" + root.string(),
        [root, kind](Provider& provider) { return provider.SequenceContinueArgv(root, kind); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string) { onSuccess(); }, std::move(onError));
}

void Runner::RequestSequenceAbort(const Repo& repo, const std::string& kind, std::function<void()> onSuccess,
                                  std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "abort", "sequence:" + root.string(),
        [root, kind](Provider& provider) { return provider.SequenceAbortArgv(root, kind); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string) { onSuccess(); }, std::move(onError));
}

void Runner::RequestSequenceSkip(const Repo& repo, const std::string& kind, std::function<void()> onSuccess,
                                 std::function<void(std::string)> onError) {
    const std::filesystem::path& root = repo.root;
    RunProviderOperation(
        repo, "skip", "sequence:" + root.string(),
        [root, kind](Provider& provider) { return provider.SequenceSkipArgv(root, kind); },
        [onSuccess = std::move(onSuccess)](Provider&, std::string) { onSuccess(); }, std::move(onError));
}

void Runner::RequestFileDiffText(const Repo& repo, const std::filesystem::path& path, bool staged,
                                 std::function<void(std::string)> onComplete, std::function<void(std::string)> onError) {
    const std::filesystem::path canonical = Canonical(repo, path);
    const std::string           keyPrefix = staged ? "file-diff-staged:" : "file-diff:";
    RunProviderOperation(
        repo, "diff preview", keyPrefix + canonical.string(),
        [canonical, staged](Provider& provider) {
            return staged ? provider.StagedDiffArgv(canonical) : provider.DiffArgv(canonical);
        },
        [onComplete = std::move(onComplete)](Provider&, std::string output) { onComplete(std::move(output)); },
        std::move(onError));
}

} // namespace ned::editor::vcs
