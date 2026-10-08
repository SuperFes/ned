//
// Stands in for ssh in the SshMaster tests, against the local machine.
//
//  - A master (-M) binds a Unix socket at the -S path and answers on it
//    until a client asks it to exit.
//  - `-O exit` asks the master at -S to exit.
//  - A session connects to the master first, failing as ssh would when
//    there's none, then runs its command line through /bin/sh -c, so the
//    remote quoting is parsed by a real shell.
//
// Destination "denied" fails login the way a refused key does; a master
// to "silent" never finishes logging in; a session on "chatty" prints to
// stdout ahead of its command, as a login script can.
//
// Without BatchMode=yes, a master asks through $SSH_ASKPASS as ssh does:
// "password" takes "hunter2" within three tries, "unknown-host" asks to
// trust its key, and "touch" shows a notice it withdraws by killing the
// askpass program once its key is "touched".
//
// With $NED_FAKE_SSH_LOG set, each session appends its command line there.
//

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include <csignal>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::string controlPath;

int Fail(const std::string& message) {
    std::fprintf(stderr, "%s\n", message.c_str());
    return 255;
}

std::string Unescape(std::string_view path) {
    std::string unescaped;
    for (std::size_t i = 0; i < path.size(); ++i) {
        unescaped += path[i];
        if (path[i] == '%' && i + 1 < path.size() && path[i + 1] == '%') {
            ++i;
        }
    }
    return unescaped;
}

sockaddr_un Address() {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    controlPath.copy(address.sun_path, sizeof address.sun_path - 1);
    return address;
}

// Sends request to the master and waits for its answer.
bool AskMaster(std::string_view request) {
    const int         fd      = ::socket(AF_UNIX, SOCK_STREAM, 0);
    const sockaddr_un address = Address();
    if (fd < 0 || ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0) {
        return false;
    }
    char       reply[8];
    const bool answered = ::write(fd, request.data(), request.size()) == static_cast<ssize_t>(request.size()) && ::read(fd, reply, sizeof reply) > 0;
    ::close(fd);
    return answered;
}

int RunMaster() {
    std::signal(SIGTERM, [](int) {
        ::unlink(controlPath.c_str());
        ::_exit(0);
    });
    const int         listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
    const sockaddr_un address  = Address();
    if (listener < 0 || ::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0 || ::listen(listener, 8) != 0) {
        return Fail("fake ssh: cannot listen on " + controlPath);
    }
    while (true) {
        const int client = ::accept(listener, nullptr, nullptr);
        if (client < 0) {
            continue;
        }
        char          request[16];
        const ssize_t length = ::read(client, request, sizeof request);
        const bool    exit   = length > 0 && std::string_view(request, static_cast<std::size_t>(length)) == "exit";
        if (exit) {
            ::unlink(controlPath.c_str());
        }
        (void)!::write(client, "ok", 2);
        ::close(client);
        if (exit) {
            return 0;
        }
    }
}

pid_t SpawnAskpass(const std::string& prompt, const char* hint, int& output) {
    const char* askpass = std::getenv("SSH_ASKPASS");
    int         pipeFds[2];
    if (askpass == nullptr || ::pipe(pipeFds) != 0) {
        return -1;
    }
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(pipeFds[1], STDOUT_FILENO);
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        if (hint != nullptr) {
            ::setenv("SSH_ASKPASS_PROMPT", hint, 1);
        }
        ::execl(askpass, askpass, prompt.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::close(pipeFds[1]);
    output = pipeFds[0];
    return pid;
}

// The answer, without its newline, or nullopt when the askpass program
// cancelled.
std::optional<std::string> Ask(const std::string& prompt) {
    int         output = -1;
    const pid_t pid    = SpawnAskpass(prompt, nullptr, output);
    if (pid < 0) {
        return std::nullopt;
    }
    std::string answer;
    char        buffer[256];
    for (ssize_t length = 0; (length = ::read(output, buffer, sizeof buffer)) > 0;) {
        answer.append(buffer, static_cast<std::size_t>(length));
    }
    ::close(output);
    int status = 0;
    ::waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return std::nullopt;
    }
    if (answer.ends_with('\n')) {
        answer.pop_back();
    }
    return answer;
}

// How a master that may ask logs in: true to go on, false having failed.
bool LogIn(std::string_view destination) {
    if (destination == "password") {
        for (int tries = 0; tries < 3; ++tries) {
            const std::optional<std::string> answer = Ask("fake@password's password: ");
            if (!answer) {
                break;
            }
            if (*answer == "hunter2") {
                return true;
            }
            std::fprintf(stderr, "Permission denied, please try again.\n");
        }
        std::fprintf(stderr, "fake@password: Permission denied (password).\n");
        return false;
    }
    if (destination == "unknown-host") {
        const std::optional<std::string> answer = Ask("The authenticity of host 'unknown-host (127.0.0.1)' can't be established.\n"
                                                      "ED25519 key fingerprint is SHA256:fake.\n"
                                                      "Are you sure you want to continue connecting (yes/no/[fingerprint])? ");
        if (answer == "yes") {
            std::fprintf(stderr, "Warning: Permanently added 'unknown-host' (ED25519) to the list of known hosts.\n");
            return true;
        }
        std::fprintf(stderr, "Host key verification failed.\n");
        return false;
    }
    if (destination == "touch") {
        int         output = -1;
        const pid_t pid    = SpawnAskpass("Confirm user presence for key ED25519-SK SHA256:fake", "none", output);
        ::usleep(200 * 1000);
        if (pid > 0) {
            ::kill(pid, SIGTERM);
            ::waitpid(pid, nullptr, 0);
            ::close(output);
        }
        return true;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    bool        master = false;
    bool        batch  = false;
    std::string control;
    int         i = 1;
    for (; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--") {
            ++i;
            break;
        }
        if (arg == "-S" || arg == "-o" || arg == "-O") {
            if (i + 1 >= argc) {
                return Fail("fake ssh: " + std::string(arg) + " needs a value");
            }
            const std::string_view value = argv[++i];
            if (arg == "-S") {
                controlPath = Unescape(value);
            }
            else if (arg == "-O") {
                control = value;
            }
            else if (value == "BatchMode=yes") {
                batch = true;
            }
        }
        else if (arg == "-M") {
            master = true;
        }
    }
    if (i >= argc) {
        return Fail("fake ssh: no destination");
    }
    const std::string_view destination = argv[i++];
    std::string            command;
    for (; i < argc; ++i) {
        command += (command.empty() ? "" : " ") + std::string(argv[i]);
    }

    if (destination == "denied") {
        return Fail("fake@denied: Permission denied (publickey).");
    }
    if (master) {
        if (destination == "silent") {
            while (true) {
                ::pause();
            }
        }
        if (batch && (destination == "password" || destination == "unknown-host")) {
            return Fail(destination == "password" ? "fake@password: Permission denied (password)." : "Host key verification failed.");
        }
        if (!batch && !LogIn(destination)) {
            return 255;
        }
        return RunMaster();
    }
    if (!AskMaster(control == "exit" ? "exit" : "session")) {
        return Fail("Control socket connect(" + controlPath + "): No such file or directory");
    }
    if (!control.empty()) {
        return 0;
    }
    if (command.empty()) {
        return Fail("fake ssh: a session needs a command");
    }
    if (const char* log = std::getenv("NED_FAKE_SSH_LOG"); log != nullptr && *log != '\0') {
        if (std::FILE* file = std::fopen(log, "a")) {
            std::fprintf(file, "%s\n", command.c_str());
            std::fclose(file);
        }
    }
    if (destination == "chatty") {
        std::fputs("Welcome to chatty\nno newline here", stdout);
        std::fflush(stdout);
    }
    ::execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    return Fail("fake ssh: cannot exec /bin/sh");
}
