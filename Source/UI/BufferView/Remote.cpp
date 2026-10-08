#include <algorithm>
#include <chrono>
#include <cstdio>
#include <expected>
#include <set>
#include <string>
#include <vector>

#include "Editor/BufferSave.h"
#include "Editor/DiagnosticsLog.h"
#include "Editor/Format.h"
#include "Editor/FormatOnSave.h"
#include "Editor/Host/SearchCodec.h"
#include "Editor/ModeOverrides.h"
#include "Editor/NextError.h"
#include "Editor/Remote/Fs.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/Remote/KnownHosts.h"
#include "Editor/Remote/RemoteBuffer.h"
#include "Editor/Remote/Search.h"
#include "UI/BufferView.h"
#include "UI/RemotePanel.h"

namespace ned::ui {

namespace {

    std::string Seconds(std::chrono::steady_clock::duration elapsed) {
        char text[32];
        std::snprintf(text, sizeof text, "%.1f s", std::chrono::duration<double>(elapsed).count());
        return text;
    }

    std::string CountMatches(std::size_t count) {
        return std::to_string(count) + (count == 1 ? " match" : " matches");
    }

    // A whole-buffer replace, as the local format command's, but one undo
    // step. False when the text is already that.
    bool ReplaceWithFormatted(text::Buffer& buffer, const std::string& formatted) {
        if (buffer.Text() == formatted) {
            return false;
        }
        buffer.BeginUndoGroup();
        buffer.DeleteRange(0, buffer.Size());
        buffer.InsertAt(0, formatted);
        buffer.EndUndoGroup();
        return true;
    }

} // namespace

void BufferView::SetRemoteHosts(editor::remote::Hosts* remoteHosts) {
    remoteHosts_ = remoteHosts;
}

void BufferView::SetRemotePanel(RemotePanel* panel) {
    remotePanel_ = panel;
}

void BufferView::BeginRemoteOpenRoot() {
    StartInteractiveSession(editor::InteractiveRequest::RemoteOpenRoot);
}

void BufferView::ConnectToHost(const std::string& destination) {
    if (remoteHosts_ == nullptr) {
        statusMessage_ = "No remote hosts.";
        return;
    }
    if (remoteHosts_->Find(destination) != nullptr) {
        statusMessage_ = "Already connected to " + destination;
        return;
    }
    statusMessage_ = "Connecting to " + destination + "...";
    remoteHosts_->Connect(destination, lifetime_.Bind([this, destination, started = std::chrono::steady_clock::now()](
                                                          const std::expected<editor::remote::Connection*, std::string>& connection) {
        if (!connection) {
            statusMessage_ = "Couldn't connect to " + destination + ": " + connection.error();
            return;
        }
        statusMessage_ = "Connected to " + destination + " in " + Seconds(std::chrono::steady_clock::now() - started) +
                         ((*connection)->Agent().uploaded ? " (installed the agent)" : "");
    }));
}

void BufferView::DisconnectFromHost(const std::string& destination) {
    if (remoteHosts_ == nullptr) {
        statusMessage_ = "No remote hosts.";
        return;
    }
    if (remoteSearch_ && remoteSearch_->root.destination == destination) {
        CancelRemoteSearch();
    }
    statusMessage_ = remoteHosts_->Disconnect(destination) ? "Disconnected from " + destination : "Not connected to " + destination;
}

void BufferView::WithRemoteHost(const std::string& destination, std::function<void(editor::remote::Connection&)> then) {
    if (remoteHosts_ == nullptr) {
        statusMessage_ = "No remote hosts.";
        return;
    }
    remoteHosts_->Connect(destination, lifetime_.Bind([this, destination, then = std::move(then)](
                                                          const std::expected<editor::remote::Connection*, std::string>& connection) {
        if (!connection) {
            statusMessage_ = "Couldn't connect to " + destination + ": " + connection.error();
            return;
        }
        then(**connection);
    }));
}

void BufferView::CreateRemoteDirectory(const editor::remote::Location& directory) {
    WithRemoteHost(directory.destination, [this, directory](editor::remote::Connection& connection) {
        editor::remote::MakeDirectory(connection, directory.path, lifetime_.Bind([this, directory](editor::remote::FsResult<editor::host::FileStat> made) {
            if (!made) {
                ReportError("Couldn't create " + directory.ToString() + ": " + made.error().message);
                return;
            }
            statusMessage_ = "Created directory " + directory.ToString();
            if (remotePanel_ != nullptr) {
                remotePanel_->Changed(directory);
            }
        }));
    });
}

void BufferView::RenameRemotePath(const editor::remote::Location& from, const editor::remote::Location& to) {
    if (from.destination != to.destination) {
        ReportError("Can't move " + from.ToString() + " to another host");
        return;
    }
    WithRemoteHost(from.destination, [this, from, to](editor::remote::Connection& connection) {
        editor::remote::RenamePath(connection, {.from = from.path, .to = to.path}, lifetime_.Bind([this, from, to](editor::remote::FsResult<void> renamed) {
            if (!renamed) {
                ReportError("Couldn't rename " + from.ToString() + ": " + renamed.error().message);
                return;
            }
            // Buffers on the file itself, or on files under a renamed directory.
            for (const auto& candidate : bufferList_.Buffers()) {
                const std::optional<text::RemoteFile>& remote = candidate->Remote();
                if (!remote || remote->destination != from.destination) {
                    continue;
                }
                if (remote->path == from.path) {
                    candidate->SetRemote(to);
                    candidate->Rename(text::RemoteBufferName(to));
                    editor::ClearModeCacheFor(*candidate);
                }
                else if (const std::filesystem::path relative = remote->path.lexically_relative(from.path); !relative.empty() && *relative.begin() != "..") {
                    candidate->SetRemote({.destination = to.destination, .path = to.path / relative});
                    editor::ClearModeCacheFor(*candidate);
                }
            }
            statusMessage_ = "Renamed to " + to.ToString();
            if (remotePanel_ != nullptr) {
                remotePanel_->Changed(from);
                remotePanel_->Changed(to);
            }
        }));
    });
}

void BufferView::DeleteRemotePath(const editor::remote::Location& target) {
    WithRemoteHost(target.destination, [this, target](editor::remote::Connection& connection) {
        editor::remote::RemovePath(connection, target.path, lifetime_.Bind([this, target](editor::remote::FsResult<void> removed) {
            if (!removed) {
                ReportError("Couldn't delete " + target.ToString() + ": " + removed.error().message);
                return;
            }
            statusMessage_ = "Deleted " + target.ToString();
            if (remotePanel_ != nullptr) {
                remotePanel_->Changed(target);
            }
        }));
    });
}

void BufferView::VisitRemoteFile(const editor::remote::Location& file, std::optional<std::size_t> line, bool createMissing) {
    VisitRemoteFile(
        file,
        [this, line](text::Buffer& buffer) {
            if (line) {
                buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(*line - 1, 0));
                if (&activeBuffer_.Get() == &buffer) {
                    viewport_.ScrollToShowPoint();
                }
            }
        },
        createMissing);
}

void BufferView::JumpToFileLine(const text::FileRef& file, std::size_t line) {
    if (const auto* remote = std::get_if<text::RemoteFile>(&file)) {
        VisitRemoteFile(*remote, line, /*createMissing=*/false);
        return;
    }
    JumpToPathLine(std::get<std::filesystem::path>(file), line);
}

void BufferView::VisitRemoteFile(const editor::remote::Location& file, std::function<void(text::Buffer& buffer)> arrive, bool createMissing) {
    if (text::Buffer* open = bufferList_.FindByRemote(file)) {
        activeBuffer_.Set(*open);
        arrive(*open);
        return;
    }
    if (remoteHosts_ == nullptr) {
        statusMessage_ = "No remote hosts.";
        return;
    }
    text::Buffer& placeholder = bufferList_.AddRemotePlaceholder(file);
    activeBuffer_.Set(placeholder);
    statusMessage_ = "Reading " + file.ToString() + "...";
    editor::remote::LoadRemoteBuffer(*remoteHosts_, bufferList_, placeholder, createMissing, lifetime_.Token(),
                                     [this, file, arrive = std::move(arrive)](text::Buffer& buffer, const editor::remote::RemoteLoadOutcome& outcome) {
                                         if (!outcome.loaded) {
                                             CloseBufferNow(buffer);
                                             ReportError("Couldn't open " + file.ToString() + ": " + outcome.loaded.error());
                                             return;
                                         }
                                         arrive(buffer);
                                         statusMessage_ = (*outcome.loaded == editor::remote::RemoteLoaded::Read ? "Opened " : "(New file) ") +
                                                          file.ToString();
                                     });
}

void BufferView::RevertRemoteBuffer(text::Buffer& buffer, std::optional<text::Charset> charset) {
    if (remoteHosts_ == nullptr) {
        statusMessage_ = "No remote hosts.";
        return;
    }
    const std::string where = buffer.Remote()->ToString();
    if (buffer.IsLoading()) {
        if (!buffer.LoadFailure()) {
            statusMessage_ = where + " is still loading.";
            return;
        }
        if (charset) {
            statusMessage_ = where + " isn't loaded yet; revert-buffer reads it first.";
            return;
        }
        buffer.MarkLoading();
        statusMessage_ = "Reading " + where + "...";
        editor::remote::LoadRemoteBuffer(*remoteHosts_, bufferList_, buffer, false, lifetime_.Token(),
                                         [this, where](text::Buffer& loading, const editor::remote::RemoteLoadOutcome& outcome) {
                                             if (!outcome.loaded) {
                                                 loading.MarkLoadFailed(outcome.loaded.error());
                                                 ReportError("Couldn't open " + where + ": " + outcome.loaded.error());
                                                 return;
                                             }
                                             statusMessage_ = "Opened " + where;
                                         });
        return;
    }
    const bool discarded = buffer.Modified();
    statusMessage_       = "Reading " + where + "...";
    editor::remote::ReloadRemoteBuffer(
        *remoteHosts_, bufferList_, buffer, charset, lifetime_.Token(),
        [this, where, charset, discarded](text::Buffer&, const std::optional<std::string>& failure) {
            if (failure) {
                ReportError("Couldn't reload " + where + ": " + *failure);
                return;
            }
            statusMessage_ = "Reloaded " + where + (charset ? " as " + std::string(text::CharsetName(*charset)) : "") + "." +
                             (discarded ? " Undo brings back your edits." : "");
        });
}

void BufferView::StartRemoteSearch(const editor::remote::Location& root, const std::string& pattern) {
    if (remoteHosts_ == nullptr) {
        statusMessage_ = "No remote hosts.";
        return;
    }
    CancelRemoteSearch();
    const std::uint64_t generation = ++remoteSearchGeneration_;
    remoteSearch_.emplace(RemoteSearchRun{.generation = generation,
                                          .root       = root,
                                          .pattern    = pattern,
                                          .requestId  = std::nullopt,
                                          .results    = {},
                                          .matches    = 0,
                                          .started    = std::chrono::steady_clock::now()});
    if (editor::remote::Connection* live = remoteHosts_->Find(root.destination)) {
        SendRemoteSearch(generation, *live);
        return;
    }
    statusMessage_ = "Connecting to " + root.destination + "...";
    remoteHosts_->Connect(root.destination, lifetime_.Bind([this, generation, destination = root.destination](
                                                               const std::expected<editor::remote::Connection*, std::string>& connection) {
        if (!remoteSearch_ || remoteSearch_->generation != generation) {
            return;
        }
        if (!connection) {
            remoteSearch_.reset();
            statusMessage_ = "Couldn't connect to " + destination + ": " + connection.error();
            return;
        }
        SendRemoteSearch(generation, **connection);
    }));
}

void BufferView::SendRemoteSearch(std::uint64_t generation, editor::remote::Connection& connection) {
    statusMessage_         = "Searching " + remoteSearch_->root.ToString() + "...";
    const std::uint64_t id = editor::remote::SearchProject(
        connection, editor::host::SearchRequest{.root = remoteSearch_->root.path, .pattern = remoteSearch_->pattern, .live = {}},
        lifetime_.Bind([this, generation](const std::vector<editor::SearchMatch>& matches) { AppendRemoteMatches(generation, matches); }),
        lifetime_.Bind([this, generation](const std::optional<std::string>& failure) { FinishRemoteSearch(generation, failure); }));
    // A connection already closing answers at once, ending the search.
    if (remoteSearch_ && remoteSearch_->generation == generation) {
        remoteSearch_->requestId = id;
    }
}

void BufferView::AppendRemoteMatches(std::uint64_t generation, const std::vector<editor::SearchMatch>& matches) {
    if (!remoteSearch_ || remoteSearch_->generation != generation) {
        return;
    }
    RemoteSearchRun& run = *remoteSearch_;
    std::string      lines;
    for (const editor::SearchMatch& match : matches) {
        lines += run.root.destination + ":" + match.file.string() + ":" + std::to_string(match.lineNumber) + ": " + match.lineText + "\n";
    }

    if (run.results.Empty()) {
        text::Buffer& results = bufferList_.CreateBuffer("*search results*");
        results.InsertAtPoint(lines);
        results.SetPoint(0);
        results.SetReadOnly(true);
        run.results = text::BufferIdentity(results);
        editor::SetLastResultsBuffer(results.Name());
        // Not out from under a prompt the user opened while connecting.
        if (inputMode_ == InputMode::Normal) {
            activeBuffer_.Set(results);
        }
    }
    else if (text::Buffer* results = bufferList_.Find(run.results)) {
        results->AppendWhileReadOnly(lines);
    }
    else {
        CancelRemoteSearch();
        statusMessage_ = "Stopped the remote search: its results buffer was closed.";
        return;
    }
    run.matches += matches.size();
    statusMessage_ = "Searching " + run.root.ToString() + "... " + CountMatches(run.matches);
}

void BufferView::FinishRemoteSearch(std::uint64_t generation, const std::optional<std::string>& failure) {
    if (!remoteSearch_ || remoteSearch_->generation != generation) {
        return;
    }
    const RemoteSearchRun run = std::move(*remoteSearch_);
    remoteSearch_.reset();
    const std::string what    = "\"" + run.pattern + "\" on " + run.root.ToString();
    const std::string elapsed = Seconds(std::chrono::steady_clock::now() - run.started);
    if (failure) {
        ReportError("Remote search for " + what + " failed after " + CountMatches(run.matches) + ": " + *failure);
    }
    else if (run.matches == 0) {
        statusMessage_ = "No matches for " + what + " (" + elapsed + ")";
    }
    else {
        statusMessage_ = CountMatches(run.matches) + " for " + what + " in " + elapsed;
    }
}

std::vector<std::string> BufferView::RemoteHostCandidates() const {
    std::vector<std::string> hosts;
    if (remoteHosts_ != nullptr) {
        hosts = remoteHosts_->Destinations();
    }
    for (const std::string& root : promptHistory_.Entries(HistoryKeyForInputMode(InputMode::RemoteSearchRoot))) {
        if (const std::optional<editor::remote::Location> location = editor::remote::ParseLocation(root)) {
            hosts.push_back(location->destination);
        }
    }
    const std::vector<std::string>& connected = promptHistory_.Entries(HistoryKeyForInputMode(InputMode::RemoteConnect));
    hosts.insert(hosts.end(), connected.begin(), connected.end());
    const std::vector<std::string> known = editor::remote::KnownHosts(editor::remote::DefaultHostSources());
    hosts.insert(hosts.end(), known.begin(), known.end());

    std::vector<std::string> unique;
    std::set<std::string>    seen;
    for (std::string& host : hosts) {
        if (seen.insert(host).second) {
            unique.push_back(std::move(host));
        }
    }
    return unique;
}

std::optional<std::string> BufferView::TypedRemoteHost() const {
    if (inputMode_ == InputMode::RemoteConnect) {
        return prompt_->Text();
    }
    // Past the host, RemotePathCompletions takes over.
    if ((inputMode_ == InputMode::RemoteSearchRoot || inputMode_ == InputMode::RemoteOpenRoot) && prompt_->Text().find(":/") == std::string::npos) {
        return prompt_->Text().substr(0, prompt_->Text().find(':'));
    }
    return std::nullopt;
}

std::vector<std::string> BufferView::RemoteHostCompletions() const {
    if (inputMode_ == InputMode::RemoteDisconnect) {
        return editor::remote::MatchHosts(remoteHostCandidates_, prompt_->Text());
    }
    const std::optional<std::string> typed = TypedRemoteHost();
    if (!typed) {
        return {};
    }
    std::vector<std::string> hosts = editor::remote::MatchHosts(remoteHostCandidates_, *typed);
    if (remoteLookupTyped_ == typed) {
        for (const std::string& resolved : remoteResolvedHosts_) {
            if (std::ranges::find(hosts, resolved) == hosts.end()) {
                hosts.push_back(resolved);
            }
        }
    }
    if (inputMode_ == InputMode::RemoteSearchRoot) {
        for (std::string& host : hosts) {
            host += ":/";
        }
    }
    return hosts;
}

void BufferView::RequestRemoteHostLookup() {
    const std::optional<std::string> typed = TypedRemoteHost();
    if (remoteHosts_ == nullptr || !typed || typed == remoteLookupTyped_) {
        return;
    }
    remoteLookupTyped_ = typed;
    remoteResolvedHosts_.clear();
    if (typed->empty()) {
        remoteHosts_->Lookup().Cancel();
        return;
    }
    remoteHosts_->Lookup().Qualify(*typed, lifetime_.Bind([this, typed = *typed](std::vector<std::string> resolved) {
        if (TypedRemoteHost() != typed) {
            return;
        }
        remoteResolvedHosts_ = std::move(resolved);
        RefreshPathCompletionPopup();
    }));
}

std::optional<editor::remote::RemotePathPrefix> BufferView::TypedRemotePath() const {
    if (inputMode_ != InputMode::FindFile && inputMode_ != InputMode::RemoteSearchRoot && inputMode_ != InputMode::RemoteOpenRoot) {
        return std::nullopt;
    }
    return editor::remote::SplitRemotePathPrefix(prompt_->Text());
}

std::vector<std::string> BufferView::RemotePathCompletions() const {
    const std::optional<editor::remote::RemotePathPrefix> typed = TypedRemotePath();
    if (!typed || typed->directory != remoteListingDirectory_ || !remoteListingEntries_) {
        return {};
    }
    return editor::remote::CompleteRemotePath(*typed, *remoteListingEntries_, inputMode_ != InputMode::FindFile);
}

void BufferView::RequestRemoteListing() {
    const std::optional<editor::remote::RemotePathPrefix> typed = TypedRemotePath();
    if (remoteHosts_ == nullptr || !typed || typed->directory == remoteListingDirectory_) {
        return;
    }
    // Plenty for a prompt; a bigger directory offers nothing rather than stalling it.
    constexpr std::size_t  kMaxListedEntries = 50000;
    const text::RemoteFile directory         = typed->directory;
    remoteListingDirectory_                  = directory;
    remoteListingEntries_.reset();
    const auto settle = [this, directory](std::vector<editor::host::DirEntry> entries) {
        if (remoteListingDirectory_ != directory) {
            return;
        }
        remoteListingEntries_ = std::move(entries);
        if (TypedRemotePath()) {
            RefreshPathCompletionPopup();
        }
    };
    remoteHosts_->Connect(directory.destination, lifetime_.Bind([this, directory, settle](const std::expected<editor::remote::Connection*, std::string>& connection) {
        if (remoteListingDirectory_ != directory) {
            return;
        }
        if (!connection) {
            settle({});
            return;
        }
        editor::remote::ListDirectory(**connection, directory.path, kMaxListedEntries,
                                      lifetime_.Bind([settle](editor::remote::FsResult<std::vector<editor::host::DirEntry>> listed) {
                                          settle(listed ? std::move(*listed) : std::vector<editor::host::DirEntry>{});
                                      }));
    }));
}

void BufferView::CancelRemoteSearch() {
    if (!remoteSearch_) {
        return;
    }
    if (remoteSearch_->requestId && remoteHosts_ != nullptr) {
        if (editor::remote::Connection* live = remoteHosts_->Find(remoteSearch_->root.destination)) {
            live->Endpoint().Cancel(*remoteSearch_->requestId);
        }
    }
    remoteSearch_.reset();
}

bool BufferView::StartRemoteFormat(const text::Buffer& buffer, editor::FormatRun::Done done) {
    const std::optional<std::string> command = editor::FormatCommand();
    if (remoteHosts_ == nullptr || !command) {
        return false;
    }
    if (!remoteFormat_) {
        remoteFormat_ = std::make_unique<editor::FormatRun>(remoteHosts_->Queue());
    }
    remoteFormat_->Start(remoteHosts_, buffer.Remote(), *command, buffer.Text(), std::move(done));
    return true;
}

void BufferView::RequestRemoteFormatThenSaveBuffer(bool force) {
    text::Buffer&              buffer     = activeBuffer_.Get();
    const bool                 closeAfter = std::exchange(closeWindowAfterDeferredSave_, false);
    const text::BufferIdentity bufferId(buffer);
    const std::size_t          generation = buffer.ContentGeneration();
    statusMessage_                        = "Formatting " + buffer.Name() + "...";
    const bool started                    = StartRemoteFormat(buffer, [this, bufferId, generation, force, closeAfter](std::expected<std::string, std::string> formatted) {
        // Saved even when no longer shown: the user asked for it.
        text::Buffer* live = LiveBuffer(bufferId);
        if (live == nullptr) {
            return;
        }
        std::string note;
        if (!formatted) {
            note = " (the format command failed: " + formatted.error() + ")";
        }
        else if (live->ContentGeneration() != generation) {
            note = " (not formatted: it changed while the format command ran)";
        }
        else {
            ReplaceWithFormatted(*live, *formatted);
        }
        SaveAfterRemoteFormat(*live, force, closeAfter, note);
    });
    if (!started) {
        SaveAfterRemoteFormat(buffer, force, closeAfter, " (the format command failed: no remote hosts)");
    }
}

void BufferView::SaveAfterRemoteFormat(text::Buffer& buffer, bool force, bool closeAfter, const std::string& note) {
    if (!note.empty()) {
        // The save's own outcome replaces the status line soon after.
        editor::LogMessage(editor::LogCategory::General, editor::LogSeverity::Warning, "saving " + buffer.Name() + note);
    }
    bool wrote = false;
    try {
        editor::WriteBufferToDisk(buffer, editor::SaveDispatch::Automatic, force);
        statusMessage_ = (buffer.IsSaving() ? "Saving " + buffer.Name() + "..." : "Wrote " + buffer.Name()) + note;
        wrote          = true;
    }
    catch (const std::exception& e) {
        statusMessage_ = e.what();
    }
    if (wrote && closeAfter) {
        CloseVimWindow(false); // may destroy *this* -- nothing after
        return;
    }
    RequestDiffForCurrentBuffer();
    viewport_.ScrollToShowPoint();
}

void BufferView::RequestRemoteFormatBuffer() {
    text::Buffer&              buffer = activeBuffer_.Get();
    const text::BufferIdentity bufferId(buffer);
    const std::size_t          generation = buffer.ContentGeneration();
    const auto                 finish     = [this, bufferId, generation](std::expected<std::string, std::string> formatted) {
        if (!bufferId.Is(activeBuffer_.Get())) {
            return; // the user moved on
        }
        text::Buffer& buffer = activeBuffer_.Get();
        if (buffer.ContentGeneration() != generation) {
            statusMessage_ = buffer.Name() + " changed while the format command ran -- not formatted.";
            return;
        }
        if (formatted) {
            statusMessage_ = ReplaceWithFormatted(buffer, *formatted) ? "Formatted " + buffer.Name() : buffer.Name() + " is already formatted.";
            viewport_.ScrollToShowPoint();
            return;
        }
        // format-buffer always does something, as it does locally.
        const bool changed = editor::ApplyNativeFormat(buffer, &mode_);
        statusMessage_     = "The format command failed: " + formatted.error() + (changed ? " -- formatted " + buffer.Name() + " natively." : ".");
        viewport_.ScrollToShowPoint();
    };
    statusMessage_ = "Formatting " + buffer.Name() + "...";
    if (!StartRemoteFormat(buffer, finish)) {
        finish(std::unexpected(std::string("no remote hosts")));
    }
}

} // namespace ned::ui
