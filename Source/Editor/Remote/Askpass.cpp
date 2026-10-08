#include "Askpass.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ned::editor::remote {

namespace {

    // A request is the hint and the prompt, each ended by a NUL; neither
    // can hold one, since both arrive as C strings. A reply is '+' and the
    // answer, or '-' alone.
    constexpr char kAnswered  = '+';
    constexpr char kCancelled = '-';

    std::optional<sockaddr_un> Address(const std::filesystem::path& path) {
        sockaddr_un       address{};
        const std::string text = path.string();
        if (text.size() >= sizeof address.sun_path) {
            return std::nullopt;
        }
        address.sun_family = AF_UNIX;
        text.copy(address.sun_path, text.size());
        return address;
    }

    bool WriteAll(int fd, std::string_view bytes) {
        while (!bytes.empty()) {
            const ssize_t written = ::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
            if (written < 0 && errno == EINTR) {
                continue;
            }
            if (written <= 0) {
                return false;
            }
            bytes.remove_prefix(static_cast<std::size_t>(written));
        }
        return true;
    }

} // namespace

AskpassKind ClassifyAskpass(std::string_view hint, std::string_view prompt) {
    if (hint == "confirm") {
        return AskpassKind::Confirm;
    }
    if (hint == "none") {
        return AskpassKind::Notice;
    }
    // ssh's host-key question ends "(yes/no/[fingerprint])?".
    if (prompt.find("(yes/no") != std::string_view::npos) {
        return AskpassKind::Text;
    }
    return AskpassKind::Secret;
}

int RunAskpassRelay(const std::filesystem::path& socketPath, std::string_view prompt, std::string_view hint, int output) {
    const std::optional<sockaddr_un> address = Address(socketPath);
    const int                        fd      = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (!address || fd < 0) {
        if (fd >= 0) {
            ::close(fd);
        }
        return 1;
    }
    std::string request(hint);
    request += '\0';
    request += prompt;
    request += '\0';
    std::string reply;
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&*address), sizeof *address) == 0 && WriteAll(fd, request)) {
        char buffer[1024];
        for (ssize_t length = 0; (length = ::read(fd, buffer, sizeof buffer)) != 0;) {
            if (length < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            reply.append(buffer, static_cast<std::size_t>(length));
        }
    }
    ::close(fd);
    if (reply.empty() || reply.front() != kAnswered) {
        return 1;
    }
    std::string answer = reply.substr(1);
    answer += '\n';
    const bool printed = [&] {
        for (std::string_view rest = answer; !rest.empty();) {
            const ssize_t written = ::write(output, rest.data(), rest.size());
            if (written < 0 && errno == EINTR) {
                continue;
            }
            if (written <= 0) {
                return false;
            }
            rest.remove_prefix(static_cast<std::size_t>(written));
        }
        return true;
    }();
    return printed ? 0 : 1;
}

AskpassRequest::AskpassRequest(int fd, std::string prompt, std::string hint) noexcept : fd_(fd), prompt_(std::move(prompt)), hint_(std::move(hint)) {
}

AskpassRequest::~AskpassRequest() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

AskpassRequest::AskpassRequest(AskpassRequest&& other) noexcept : fd_(std::exchange(other.fd_, -1)), prompt_(std::move(other.prompt_)), hint_(std::move(other.hint_)) {
}

AskpassRequest& AskpassRequest::operator=(AskpassRequest&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_     = std::exchange(other.fd_, -1);
        prompt_ = std::move(other.prompt_);
        hint_   = std::move(other.hint_);
    }
    return *this;
}

const std::string& AskpassRequest::Prompt() const noexcept {
    return prompt_;
}

const std::string& AskpassRequest::Hint() const noexcept {
    return hint_;
}

int AskpassRequest::Fd() const noexcept {
    return fd_;
}

void AskpassRequest::Reply(const AskpassAnswer& answer) noexcept {
    if (fd_ < 0) {
        return;
    }
    try {
        (void)WriteAll(fd_, answer ? kAnswered + *answer : std::string(1, kCancelled));
    }
    catch (const std::bad_alloc&) {
        // The relay reads the hang-up as a cancellation.
    }
    ::close(std::exchange(fd_, -1));
}

AskpassListener::AskpassListener(std::filesystem::path path) : path_(std::move(path)), fd_(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) {
    if (fd_ < 0) {
        throw std::runtime_error(std::string("askpass socket: ") + std::strerror(errno));
    }
    const std::optional<sockaddr_un> address = Address(path_);
    std::error_code                  ec;
    std::filesystem::remove(path_, ec);
    if (!address || ::bind(fd_, reinterpret_cast<const sockaddr*>(&*address), sizeof *address) != 0 || ::listen(fd_, 4) != 0) {
        const std::string reason = address ? std::strerror(errno) : "path too long";
        ::close(fd_);
        std::filesystem::remove(path_, ec);
        throw std::runtime_error("cannot listen on " + path_.string() + ": " + reason);
    }
}

AskpassListener::~AskpassListener() {
    ::close(fd_);
    std::error_code ec;
    std::filesystem::remove(path_, ec);
}

const std::filesystem::path& AskpassListener::Path() const noexcept {
    return path_;
}

int AskpassListener::Fd() const noexcept {
    return fd_;
}

std::optional<AskpassRequest> AskpassListener::Accept(std::chrono::milliseconds timeout) {
    const int fd = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) {
        return std::nullopt;
    }
    std::string request;
    const auto  deadline = std::chrono::steady_clock::now() + timeout;
    while (std::ranges::count(request, '\0') < 2) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        pollfd     readable{.fd = fd, .events = POLLIN, .revents = 0};
        char       buffer[1024];
        ssize_t    length = 0;
        if (left.count() <= 0 || ::poll(&readable, 1, static_cast<int>(left.count())) <= 0 || (length = ::read(fd, buffer, sizeof buffer)) <= 0) {
            ::close(fd);
            return std::nullopt;
        }
        request.append(buffer, static_cast<std::size_t>(length));
    }
    const std::size_t hintEnd   = request.find('\0');
    const std::size_t promptEnd = request.find('\0', hintEnd + 1);
    return AskpassRequest(fd, request.substr(hintEnd + 1, promptEnd - hintEnd - 1), request.substr(0, hintEnd));
}

} // namespace ned::editor::remote
