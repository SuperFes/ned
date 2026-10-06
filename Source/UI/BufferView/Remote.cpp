#include <chrono>
#include <cstdio>
#include <expected>
#include <string>
#include <vector>

#include "Editor/Host/SearchCodec.h"
#include "Editor/NextError.h"
#include "Editor/Remote/Hosts.h"
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

void BufferView::StartRemoteSearch(const editor::remote::Location& root, const std::string& pattern) {
    if (remoteHosts_ == nullptr) {
        statusMessage_ = "No remote hosts.";
        return;
    }
    CancelRemoteSearch();
    lastRemoteSearchRoot_          = root.ToString();
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
        statusMessage_ = CountMatches(run.matches) + " for " + what + " in " + elapsed + " -- remote files can't be opened yet";
    }
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
