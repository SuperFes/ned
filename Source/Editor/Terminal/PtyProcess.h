//
// Terminal-panel follow-up. Owns one shell subprocess running on a real pty
// (forkpty) and streams the master side's raw output to a caller-supplied
// callback -- deliberately a near-copy of Tasks/TaskProcess (read that file's
// and Lsp/LspClient.h's header comments for the full threading/lifetime
// reasoning; none of it is repeated here because almost none of it differs),
// the same "mirror exactly, differ only where it must" convention
// Dap/DapClient.h already established. The differences that must exist:
//
// - Spawn is forkpty + execve, not ChildProcess's posix_spawn + pipes: a
//   terminal application needs a controlling tty and session leadership,
//   which posix_spawn cannot establish. The resulting master fd pair
//   (dup(master), master) is then adopted via ChildProcess's raw-fd
//   constructor, which provides WriteAll and the close-fds-then-grace-then-
//   SIGKILL destructor unchanged -- closing the master is what delivers the
//   shell its SIGHUP.
// - The read loop calls ::read directly instead of ChildProcess::ReadSome: a
//   pty master reports the child's exit as EIO, not a clean 0-byte EOF, and
//   ReadSome would turn that routine shutdown signal into a thrown error.
// - Resize (TIOCSWINSZ on the master) exists at all -- the kernel delivers
//   the foreground process group its SIGWINCH from that ioctl.
//
// Member declaration order is load-bearing exactly as in TaskProcess:
// readThread_ before child_, so child_'s destruction (fd close + child
// teardown) is what unblocks the read loop for the jthread's own join.
//

#ifndef NED_EDITOR_TERMINAL_PTYPROCESS_H
#define NED_EDITOR_TERMINAL_PTYPROCESS_H

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Editor/Process/ChildProcess.h"
#include "Editor/ProcessTimeouts.h"
#include "UI/EventLoop.h"

namespace ned::editor::terminal {

class PtyProcess {
  public:
    // Spawns argv on a fresh pty sized rows x cols, with TERM set to
    // xterm-256color in the child's environment. onOutput receives each raw
    // chunk on the main thread; onExit fires exactly once, on the main
    // thread, after the child has exited. eventLoop must outlive this
    // PtyProcess. Throws std::runtime_error if argv is empty, argv[0] can't
    // be resolved, or the pty/fork itself fails.
    PtyProcess(std::vector<std::string> argv, int rows, int cols, ned::ui::EventLoop& eventLoop,
               std::function<void(std::string_view chunk)> onOutput, std::function<void(std::optional<int> exitCode)> onExit);

    // Marks alive_ false before member destruction does the real teardown
    // work (see header comment) -- unlike TaskProcess/LspClient, a
    // PtyProcess is genuinely destroyed *mid-run* (TerminalPanel's [×]
    // close button, respawn after exit), so read-loop callbacks already
    // sitting in the EventLoop's queue would otherwise be drained against a
    // freed object later. Every posted lambda holds the shared flag and
    // checks it at drain time; flag writes and drains both happen on the
    // main thread, so no further synchronization is needed. A real,
    // user-reported intermittent shutdown hang traced to exactly this
    // use-after-free, not defensive plumbing.
    ~PtyProcess();

    PtyProcess(const PtyProcess&)            = delete;
    PtyProcess& operator=(const PtyProcess&) = delete;
    // Not movable: the background thread's lambda captures `this` directly.
    PtyProcess(PtyProcess&&)            = delete;
    PtyProcess& operator=(PtyProcess&&) = delete;

    // Sends bytes to the shell (the master side's write half). Main thread
    // only, like every other public method here. Throws std::runtime_error
    // (write-side-hang-protection follow-up) if the shell stops draining
    // its side of the pty for longer than timeout, rather than blocking the
    // whole editor on a wedged shell.
    void Write(std::string_view data, std::chrono::milliseconds timeout = SubprocessWriteTimeoutMs()) const;

    // Propagates a new terminal size to the pty (TIOCSWINSZ) -- the kernel
    // raises SIGWINCH in the child's foreground process group. The caller
    // (TerminalPanel) resizes its Emulator separately; nothing here knows
    // emulation exists.
    void Resize(int rows, int cols) const noexcept;

    // Public primarily for tests -- the same escape hatch
    // TaskProcess::DispatchOutput/DispatchExit document: the real read loop
    // reaches these via eventLoop_.Post, which needs a live Run() loop tests
    // never start.
    void DispatchOutput(std::string_view chunk);
    void DispatchExit(std::optional<int> exitCode);

    [[nodiscard]] int MasterFdForTesting() const noexcept;

  private:
    void StartReadLoop();

    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true); // see ~PtyProcess

    std::jthread          readThread_; // declared before child_ -- see header comment
    process::ChildProcess child_;

    ned::ui::EventLoop& eventLoop_;

    std::function<void(std::string_view chunk)>      onOutput_;
    std::function<void(std::optional<int> exitCode)> onExit_;
};

} // namespace ned::editor::terminal

#endif // NED_EDITOR_TERMINAL_PTYPROCESS_H
