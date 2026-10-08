//
// Terminal-panel follow-up. Owns one shell subprocess running on a real pty
// (forkpty) and streams the master side's raw output to a caller-supplied
// callback. The read loop, its main-thread marshaling and the teardown order
// live in Process/StreamedChild.h, shared with Tasks/TaskProcess. What
// differs here:
//
// - It runs on a pty (Process/Pty.h), with TERM pinned to what Emulator
//   understands.
// - Resize exists at all.
//

#ifndef NED_EDITOR_TERMINAL_PTYPROCESS_H
#define NED_EDITOR_TERMINAL_PTYPROCESS_H

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Editor/Process/ChildProcess.h"
#include "Editor/Process/StreamedChild.h"
#include "UI/EventLoop.h"

namespace ned::editor::terminal {

class PtyProcess {
  public:
    // Spawns argv on a fresh pty sized rows x cols, with TERM set to
    // xterm-256color in the child's environment. onOutput receives each raw
    // chunk on the main thread; onExit fires exactly once, on the main
    // thread, after the child has exited. eventLoop must outlive this
    // PtyProcess. Throws std::runtime_error if argv is empty, argv[0] can't
    // be resolved, or the pty/fork itself fails. `env` sets (or replaces)
    // variables in the child's environment on top of ned's own.
    PtyProcess(std::vector<std::string> argv, int rows, int cols, ned::ui::EventLoop& eventLoop,
               std::function<void(std::string_view chunk)> onOutput, std::function<void(std::optional<int> exitCode)> onExit,
               const std::vector<std::pair<std::string, std::string>>& env = {});

    PtyProcess(const PtyProcess&)            = delete;
    PtyProcess& operator=(const PtyProcess&) = delete;

    // Queues bytes for the shell, written however long it takes to read
    // them (StreamedChild::Send). Main thread only, like every other public
    // method here.
    void Write(std::string_view data);

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
    process::StreamedChild stream_;
};

} // namespace ned::editor::terminal

#endif // NED_EDITOR_TERMINAL_PTYPROCESS_H
