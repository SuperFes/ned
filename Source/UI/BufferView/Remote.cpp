#include <algorithm>
#include <chrono>
#include <cstdio>
#include <expected>
#include <set>
#include <string>
#include <vector>

#include "Editor/Host/SearchCodec.h"
#include "Editor/NextError.h"
#include "Editor/Remote/Fs.h"
#include "Editor/Remote/Hosts.h"
#include "Editor/Remote/KnownHosts.h"
#include "Editor/Remote/RemoteBuffer.h"
#include "Editor/Remote/Search.h"
#include "UI/BufferView.h"

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

} // namespace

void BufferView::SetRemoteHosts(editor::remote::Hosts* remoteHosts) {
    remoteHosts_ = remoteHosts;
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
            ReportError("Couldn't connect to " + destination + ": " + connection.error());
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

void BufferView::VisitRemoteFile(const editor::remote::Location& file, std::optional<std::size_t> line, bool createMissing) {
    const auto goToLine = [this, line](text::Buffer& buffer) {
        if (line) {
            buffer.SetPoint(buffer.ByteOffsetForLineAndColumn(*line - 1, 0));
            if (&activeBuffer_.Get() == &buffer) {
                viewport_.ScrollToShowPoint();
            }
        }
    };
    if (text::Buffer* open = bufferList_.FindByRemote(file)) {
        activeBuffer_.Set(*open);
        goToLine(*open);
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
                                     [this, file, goToLine](text::Buffer& buffer, const editor::remote::RemoteLoadOutcome& outcome) {
                                         if (!outcome.loaded) {
                                             CloseBufferNow(buffer);
                                             ReportError("Couldn't open " + file.ToString() + ": " + outcome.loaded.error());
                                             return;
                                         }
                                         goToLine(buffer);
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
            ReportError("Couldn't connect to " + destination + ": " + connection.error());
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
    if (inputMode_ == InputMode::RemoteSearchRoot && prompt_->Text().find(":/") == std::string::npos) {
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
    if (inputMode_ != InputMode::FindFile && inputMode_ != InputMode::RemoteSearchRoot) {
        return std::nullopt;
    }
    return editor::remote::SplitRemotePathPrefix(prompt_->Text());
}

std::vector<std::string> BufferView::RemotePathCompletions() const {
    const std::optional<editor::remote::RemotePathPrefix> typed = TypedRemotePath();
    if (!typed || typed->directory != remoteListingDirectory_ || !remoteListingEntries_) {
        return {};
    }
    return editor::remote::CompleteRemotePath(*typed, *remoteListingEntries_, inputMode_ == InputMode::RemoteSearchRoot);
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

} // namespace ned::ui
