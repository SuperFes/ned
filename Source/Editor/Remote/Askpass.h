//
// ssh's questions during a login -- a password, a key's passphrase, a
// keyboard-interactive code, whether to trust an unknown host key --
// answered in the editor, since the master has no terminal to ask on.
//
// ssh runs $SSH_ASKPASS with the question as its only argument and reads
// the answer from its stdout. The master's SSH_ASKPASS is ned itself, with
// NED_ASKPASS_SOCKET naming a socket SshMaster::Start listens on: that ned
// relays the question over the socket and prints the answer it gets back.
//

#ifndef NED_EDITOR_REMOTE_ASKPASS_H
#define NED_EDITOR_REMOTE_ASKPASS_H

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace ned::editor::remote {

inline constexpr const char* kAskpassSocketVariable = "NED_ASKPASS_SOCKET";

enum class AskpassKind {
    Secret,  // typed unseen: a password, a passphrase, a one-time code
    Text,    // typed in the open: whether to trust an unknown host key
    Confirm, // yes or no: using an agent key added with ssh-add -c
    Notice,  // nothing to type, shown until ssh is done: touching a security key
};

// hint is the SSH_ASKPASS_PROMPT ssh sets for the relay.
[[nodiscard]] AskpassKind ClassifyAskpass(std::string_view hint, std::string_view prompt);

struct AskpassQuestion {
    std::string destination;
    std::string prompt;
    AskpassKind kind = AskpassKind::Secret;
    // What ssh printed since the login's previous question, such as a
    // rejected password's "Permission denied, please try again."
    std::string detail;
};

// nullopt cancels the question, which usually ends the login.
using AskpassAnswer = std::optional<std::string>;

// Blocks until the question is answered, or returns soon after withdrawn
// is requested: the login ended, was cancelled, or ssh stopped waiting. A
// Notice has no answer and blocks until withdrawn.
using AskpassHandler = std::function<AskpassAnswer(const AskpassQuestion& question, const std::stop_token& withdrawn)>;

// The relay, in the process ssh started as SSH_ASKPASS: sends prompt and
// hint to socketPath and writes the answer and a newline to output.
// Returns the exit code ssh reads: 0 when answered, 1 when cancelled or
// the editor is gone.
[[nodiscard]] int RunAskpassRelay(const std::filesystem::path& socketPath, std::string_view prompt, std::string_view hint, int output);

// The editor's end of one relay's question.
class AskpassRequest {
  public:
    AskpassRequest(int fd, std::string prompt, std::string hint) noexcept;
    ~AskpassRequest();

    AskpassRequest(AskpassRequest&& other) noexcept;
    AskpassRequest& operator=(AskpassRequest&& other) noexcept;
    AskpassRequest(const AskpassRequest&)            = delete;
    AskpassRequest& operator=(const AskpassRequest&) = delete;

    [[nodiscard]] const std::string& Prompt() const noexcept;
    [[nodiscard]] const std::string& Hint() const noexcept;
    // Readable once the relay is gone: ssh kills it when it stops waiting.
    [[nodiscard]] int Fd() const noexcept;
    // Sends the answer, or the cancellation, and hangs up.
    void Reply(const AskpassAnswer& answer) noexcept;

  private:
    int         fd_;
    std::string prompt_;
    std::string hint_;
};

// Listens for relays at path, which it removes with itself. Owned by one
// thread.
class AskpassListener {
  public:
    // Throws std::runtime_error if the socket can't be bound.
    explicit AskpassListener(std::filesystem::path path);
    ~AskpassListener();

    AskpassListener(const AskpassListener&)            = delete;
    AskpassListener& operator=(const AskpassListener&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept;
    // Readable when a relay is connecting.
    [[nodiscard]] int Fd() const noexcept;
    // Once Fd() is readable: the relay's question, or nullopt when it hangs
    // up or stays silent past timeout before finishing it.
    [[nodiscard]] std::optional<AskpassRequest> Accept(std::chrono::milliseconds timeout);

  private:
    std::filesystem::path path_;
    int                   fd_;
};

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_ASKPASS_H
