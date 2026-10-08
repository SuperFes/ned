//
// The editor's ssh transport to a remote host (Docs/ClientServerProtocol.md).
// One ssh master process per host logs in once; every session after it --
// the agent, deploy commands -- multiplexes through the master's control
// socket instead of logging in again.
//
// It shells out to the user's own ssh, so ~/.ssh/config, the ssh agent and
// known_hosts all apply. Every invocation overrides what would break a
// non-interactive pipe: RemoteCommand=none and RequestTTY=no (a config that
// launches a shell otherwise refuses a command), and BatchMode=yes, since a
// password or host-key prompt has no terminal to appear on. Only a master
// given an askpass handler asks them, through it (Askpass.h).
//

#ifndef NED_EDITOR_REMOTE_SSH_H
#define NED_EDITOR_REMOTE_SSH_H

#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Process/ChildProcess.h"
#include "Editor/Remote/Askpass.h"

namespace ned::editor::remote {

struct SshOptions {
    std::string program = "ssh";
    // ssh's ConnectTimeout: the TCP connect alone.
    std::chrono::seconds connectTimeout{15};
    // How long SshMaster::Start waits for the whole login.
    std::chrono::milliseconds startTimeout{30000};
    // Keepalives, so a dropped link fails after interval * countMax.
    std::chrono::seconds aliveInterval{15};
    int                  aliveCountMax = 3;
    // Answers the master's login questions. Unset, the login runs in
    // BatchMode and fails wherever ssh would ask.
    AskpassHandler askpass;
    // What ssh runs as SSH_ASKPASS; this executable when empty.
    std::string askpassProgram;
};

class SshError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// One word of a remote command line. ssh joins its command arguments with
// spaces and the remote login shell parses the result, so anything beyond
// a conservative set of characters is single-quoted. fish reads "\\" inside
// single quotes as one backslash where POSIX shells read two.
[[nodiscard]] std::string QuoteRemoteWord(std::string_view word);
[[nodiscard]] std::string RemoteCommandLine(std::span<const std::string> argv);

// The argv for each ssh role against controlPath.
[[nodiscard]] std::vector<std::string> SshMasterArgv(const std::string& destination, const std::filesystem::path& controlPath, const SshOptions& options);
[[nodiscard]] std::vector<std::string> SshSessionArgv(const std::string& destination, const std::filesystem::path& controlPath, const SshOptions& options,
                                                      std::span<const std::string> remoteArgv);
[[nodiscard]] std::vector<std::string> SshExitArgv(const std::string& destination, const std::filesystem::path& controlPath, const SshOptions& options);

// A control socket path unique to this process, under
// EnsureRuntimeDirectory("ssh"). Unique per call, so two editors -- or two
// connections in one editor -- never share a master.
[[nodiscard]] std::filesystem::path NewControlPath();

// Asks every master whose editor is gone to exit, and removes its control
// socket. A master runs with -n under ControlPersist=no, so nothing ends it
// when an editor dies without stopping it; its login stays up until the
// link drops. Sockets of live editors are left alone. Returns how many
// were swept.
std::size_t SweepOrphanedMasters(const SshOptions& options = {});

// Owns one ssh master and its control socket. Start, Stop and destruction
// belong to one owner thread; SessionArgv is const and may be called from
// any thread once Start has returned.
class SshMaster {
  public:
    // An empty controlPath takes NewControlPath().
    explicit SshMaster(std::string destination, SshOptions options = {}, std::filesystem::path controlPath = {});
    ~SshMaster();

    SshMaster(const SshMaster&)            = delete;
    SshMaster& operator=(const SshMaster&) = delete;

    // Blocks until the master has logged in and its control socket is up.
    // Throws SshError carrying ssh's own message when the master exits
    // (a refused key, an unknown host key, an unreachable host), on
    // timeout, or once stop is requested; the master is gone either way.
    // The timeout restarts after each question the askpass handler
    // answers, so time spent answering never counts against it.
    void Start(const std::stop_token& stop = {});

    // Asks the master to exit (which ends every session through it), then
    // reaps it. Safe to call when not started, and repeatedly.
    void Stop() noexcept;

    // The argv that runs remoteArgv on the host through this master: a
    // FrameTransport's, or a one-shot command's.
    [[nodiscard]] std::vector<std::string> SessionArgv(std::span<const std::string> remoteArgv) const;

    [[nodiscard]] const std::string&           Destination() const noexcept;
    [[nodiscard]] const std::filesystem::path& ControlPath() const noexcept;

  private:
    std::string                          destination_;
    SshOptions                           options_;
    std::filesystem::path                controlPath_;
    std::optional<process::ChildProcess> master_;
};

// How a one-shot session ended and what it printed.
struct SessionResult {
    std::optional<int> exitCode; // nullopt when killed by a signal
    std::string        output;
    std::string        errors;
};

// Runs remoteArgv through master with input on its stdin, then collects
// its output, so it suits commands that print little. A command that exits
// before reading all its input still reports how it ended. Throws SshError
// when nothing moves for stallTimeout or once stop is requested, killing
// the session either way.
SessionResult RunSession(const SshMaster& master, std::span<const std::string> remoteArgv, std::string_view input = {},
                         std::chrono::milliseconds stallTimeout = std::chrono::seconds(30), const std::stop_token& stop = {});

} // namespace ned::editor::remote

#endif
