#include "Notify.h"

#include <mutex>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ned::editor::acp {

namespace {
    std::mutex               g_notifyMutex;
    std::vector<std::string> g_notifyCommand{"notify-send", "-a", "ned"};
} // namespace

void SetAcpNotifyCommand(std::vector<std::string> argv) {
    const std::lock_guard<std::mutex> lock(g_notifyMutex);
    g_notifyCommand = std::move(argv);
}

std::vector<std::string> AcpNotifyCommand() {
    const std::lock_guard<std::mutex> lock(g_notifyMutex);
    return g_notifyCommand;
}

std::vector<std::string> NotifyArgv(const std::string& title, const std::string& body) {
    std::vector<std::string> argv = AcpNotifyCommand();
    if (argv.empty() || argv.front().empty()) {
        return {};
    }
    argv.push_back(title);
    argv.push_back(body);
    return argv;
}

bool SendDesktopNotification(const std::string& title, const std::string& body) {
    std::vector<std::string> argv = NotifyArgv(title, body);
    if (argv.empty()) {
        return false;
    }
    // Built before fork: the child may only make async-signal-safe calls.
    std::vector<char*> pointers;
    pointers.reserve(argv.size() + 1);
    for (std::string& arg : argv) {
        pointers.push_back(arg.data());
    }
    pointers.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        // Off ned's terminal: anything the notifier prints would land on
        // the TUI.
        ::setsid();
        if (const int devNull = ::open("/dev/null", O_RDWR); devNull >= 0) {
            ::dup2(devNull, STDIN_FILENO);
            ::dup2(devNull, STDOUT_FILENO);
            ::dup2(devNull, STDERR_FILENO);
        }
        ::execvp(pointers[0], pointers.data());
        ::_exit(127);
    }
    std::thread([pid] {
        int status = 0;
        ::waitpid(pid, &status, 0);
    }).detach();
    return true;
}

} // namespace ned::editor::acp
