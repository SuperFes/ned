#include "HostLookup.h"

#include <condition_variable>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

#include <poll.h>

#include "Editor/Process/ChildProcess.h"
#include "Editor/Remote/KnownHosts.h"

namespace ned::editor::remote {

namespace {

    // How often a running lookup checks for cancellation.
    constexpr int kPollMs = 50;

    struct Asking {
        std::size_t           index;
        process::ChildProcess child;
        bool                  answered = false;
    };

    std::vector<bool> Resolve(const std::vector<std::string>& names, const HostLookupOptions& options, const std::stop_token& stop) {
        std::vector<bool>   resolved(names.size(), false);
        std::vector<Asking> asking;
        for (std::size_t i = 0; i < names.size(); ++i) {
            std::vector<std::string> argv = options.command;
            argv.push_back(names[i]);
            try {
                asking.push_back(Asking{.index = i, .child = process::ChildProcess(argv), .answered = false});
            }
            catch (const std::exception&) {
                // No resolver to ask counts as no answer.
            }
        }

        const auto deadline = std::chrono::steady_clock::now() + options.limit;
        while (!asking.empty() && !stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
            std::vector<pollfd> fds;
            for (const Asking& entry : asking) {
                fds.push_back(pollfd{.fd = entry.child.ReadFd(), .events = POLLIN, .revents = 0});
            }
            if (::poll(fds.data(), fds.size(), kPollMs) <= 0) {
                continue;
            }
            for (std::size_t i = fds.size(); i-- > 0;) {
                if (fds[i].revents == 0) {
                    continue;
                }
                Asking& entry = asking[i];
                try {
                    if (!entry.child.ReadSome().empty()) {
                        entry.answered = true;
                        continue;
                    }
                }
                catch (const std::exception&) {
                }
                // EOF: getent has finished, exiting 0 only for a name it found.
                resolved[entry.index] = entry.answered && entry.child.WaitForExit() == 0;
                asking.erase(asking.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
        // Whatever is left is killed and reaped with its ChildProcess.
        return resolved;
    }

} // namespace

HostLookup::HostLookup(PostQueue& postQueue, HostLookupOptions options) : postQueue_(postQueue), options_(std::move(options)) {
}

HostLookup::~HostLookup() {
    Cancel();
}

void HostLookup::Lookup(std::vector<std::string> names, Done done) {
    Cancel();
    worker_ = std::jthread([this, names = std::move(names), done = lifetime_.Bind(std::move(done))](const std::stop_token& stop) mutable {
        std::mutex                   mutex;
        std::unique_lock<std::mutex> lock(mutex);
        std::condition_variable_any  wake;
        if (wake.wait_for(lock, stop, options_.delay, [&stop] { return stop.stop_requested(); })) {
            return;
        }
        const std::vector<bool> resolved = Resolve(names, options_, stop);
        if (stop.stop_requested()) {
            return;
        }
        std::vector<std::string> found;
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (resolved[i]) {
                found.push_back(std::move(names[i]));
            }
        }
        postQueue_.Post([done = std::move(done), found = std::move(found)]() mutable { done(std::move(found)); });
    });
}

void HostLookup::Qualify(const std::string& typed, Done done) {
    std::ifstream                  in(options_.resolvConf);
    const std::vector<std::string> domains = ParseSearchDomains(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()));
    std::vector<std::string>       names;
    for (const std::string& domain : domains) {
        if (!typed.ends_with("." + domain)) {
            names.push_back(typed + "." + domain);
        }
    }
    Lookup(std::move(names), std::move(done));
}

void HostLookup::Cancel() {
    lifetime_.Revoke();
    worker_ = std::jthread();
}

} // namespace ned::editor::remote
