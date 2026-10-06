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
// to "silent" never finishes logging in.
//

#include <cstdio>
#include <string>
#include <string_view>

#include <csignal>
#include <sys/socket.h>
#include <sys/un.h>
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

} // namespace

int main(int argc, char** argv) {
    bool        master = false;
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
    ::execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    return Fail("fake ssh: cannot exec /bin/sh");
}
