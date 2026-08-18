//
// LSP client follow-up. Raw process + pipe mechanics for talking to a
// language server over stdio -- no JSON-RPC/LSP semantics here at all (see
// LspClient.h for that layer). Nothing in this codebase previously spawned a
// long-lived subprocess and communicated with it over pipes: Editor/
// FormatOnSave.cpp shells out via a one-shot, blocking std::system() through
// temp files, and Editor/Link.cpp's OpenUrl does a detached fork+exec with no
// pipes at all. This is genuinely new infrastructure, not an extension of
// either.
//
// Spawns via posix_spawn, not fork+exec -- fork duplicates this process's
// entire address space (copy-on-write, but still real page-table work for a
// process this size, with FTXUI/tree-sitter/Janet all loaded), which
// posix_spawn avoids; it's the standard lower-overhead choice for spawning a
// child process from a large parent. Also deliberately not posix_spawnp:
// posix_spawn(p)'s error reporting for "the executable doesn't exist" is not
// reliably synchronous (implementations commonly vfork-and-exec internally,
// so a failed exec in the child only surfaces later via its exit status, not
// the posix_spawn call's own return value) -- ResolveExecutable below does a
// manual $PATH search first specifically so a missing language server binary
// throws immediately, with a clear message, from this constructor, rather
// than failing silently/asynchronously.
//

#ifndef NED_EDITOR_LSP_TRANSPORT_H
#define NED_EDITOR_LSP_TRANSPORT_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

namespace ned::editor::lsp {

class Transport {
  public:
    // argv[0] is resolved against $PATH (or treated as a literal path if it
    // contains a '/', matching execvp's own convention) before spawning.
    // Throws std::runtime_error if argv is empty, the executable can't be
    // resolved/isn't executable, pipe creation fails, or posix_spawn itself
    // fails synchronously.
    explicit Transport(const std::vector<std::string>& argv);

    // Wraps already-open, already-connected file descriptors directly,
    // taking ownership of both -- for a caller that manages the underlying
    // connection itself (a test driving a raw pipe pair with no real
    // subprocess involved; in principle also a future non-stdio connection,
    // e.g. a socket). pid, if given, is reaped/killed by the destructor the
    // same way the process-spawning constructor's child is; -1 (the
    // default) means "no process to manage," skipping that logic entirely.
    Transport(int readFd, int writeFd, pid_t pid = -1) noexcept;

    ~Transport();

    Transport(Transport&& other) noexcept;
    Transport& operator=(Transport&& other) noexcept;
    Transport(const Transport&)            = delete;
    Transport& operator=(const Transport&) = delete;

    // Writes one LSP frame ("Content-Length: N\r\n\r\n" + payload) to the
    // child's stdin. Throws std::runtime_error on a write failure (e.g. the
    // child already exited and closed its stdin -- EPIPE).
    void WriteFrame(std::string_view jsonPayload) const;

    // Blocks until one full LSP frame has been read from the child's
    // stdout. Returns std::nullopt on EOF (the server exited) rather than
    // throwing -- that's an ordinary, expected outcome a caller needs to
    // detect and react to, not an exceptional one. Throws std::runtime_error
    // if a frame is malformed (missing/unparseable Content-Length).
    [[nodiscard]] std::optional<std::string> ReadFrame() const;

    [[nodiscard]] pid_t Pid() const noexcept;

  private:
    int   writeFd_ = -1;
    int   readFd_  = -1;
    pid_t pid_     = -1;
};

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_TRANSPORT_H
