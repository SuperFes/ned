//
// The host protocol's wire framing (Docs/ClientServerProtocol.md, section 2):
//
//   length: u32 big-endian | flags: u8 | payload (length bytes)
//
// Flags bit 0 marks a zstd-compressed payload; bits 1-7 are reserved and a
// set one is a protocol error. Lengths over kMaxFrameBytes are rejected
// before anything is allocated, and so is a compressed payload that would
// decompress past it. A rejected frame ends the connection: there's no
// resynchronizing inside a length-prefixed stream.
//
// Satisfies FramedConnection.h's TransportT shape, like LineTransport.
//

#ifndef NED_EDITOR_PROTOCOL_FRAMETRANSPORT_H
#define NED_EDITOR_PROTOCOL_FRAMETRANSPORT_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

#include "Editor/Process/ChildProcess.h"
#include "Editor/ProcessTimeouts.h"

namespace ned::editor::protocol {

inline constexpr std::size_t kMaxFrameBytes = std::size_t{64} << 20;
// Below this, compression costs more than it saves.
inline constexpr std::size_t   kCompressMinBytes = 1024;
inline constexpr unsigned char kFrameFlagZstd    = 0x01;
inline constexpr std::size_t   kFrameHeaderBytes = 5;

class FrameTransport {
  public:
    // Spawns argv; see LineTransport's identical constructor.
    explicit FrameTransport(const std::vector<std::string>& argv, bool captureStderr = false);

    // Takes ownership of an already-connected fd pair: a test's pipes, or an
    // agent's own stdin/stdout.
    FrameTransport(int readFd, int writeFd, pid_t pid = -1);

    FrameTransport(FrameTransport&&)                 = default;
    FrameTransport& operator=(FrameTransport&&)      = default;
    FrameTransport(const FrameTransport&)            = delete;
    FrameTransport& operator=(const FrameTransport&) = delete;

    // Whether WriteFrame compresses payloads of at least kCompressMinBytes.
    // Reading always accepts either form; only the sender is negotiated.
    void SetCompression(bool enabled) noexcept {
        compress_->store(enabled);
    }

    // Throws std::runtime_error on a failed write, a stall past stallTimeout,
    // or a payload over kMaxFrameBytes.
    void WriteFrame(std::string_view payload, std::chrono::milliseconds stallTimeout = ProtocolWriteStallTimeoutMs()) const;

    // One decompressed payload, or nullopt on a clean EOF between frames.
    // Waits unbounded for a frame to start; once it has, silence past
    // stallTimeout throws. Also throws on EOF mid-frame, a reserved flag, an
    // oversized length, or a payload zstd rejects.
    [[nodiscard]] std::optional<std::string> ReadFrame(std::chrono::milliseconds stallTimeout = ProtocolReadStallTimeoutMs()) const;

    // True once a frame (or EOF) is ready within timeout, for a reader that
    // can't wait unbounded for one to start.
    [[nodiscard]] bool WaitReadable(std::chrono::milliseconds timeout) const;

    // Whatever bytes are ready, outside the framing: for skipping what a
    // session prints before its peer starts speaking. Empty on EOF.
    [[nodiscard]] std::string ReadUnframed() const;

    void Close() noexcept;

    [[nodiscard]] pid_t              Pid() const noexcept;
    [[nodiscard]] int                StderrFd() const noexcept;
    [[nodiscard]] std::string        ReadStderr() const;
    [[nodiscard]] const std::string& ProcessLabel() const noexcept;

  private:
    process::ChildProcess child_;
    std::string           processLabel_;
    // Behind pointers so the transport stays movable.
    std::unique_ptr<std::mutex>        writeMutex_ = std::make_unique<std::mutex>();
    std::unique_ptr<std::atomic<bool>> compress_   = std::make_unique<std::atomic<bool>>(false);
};

// The frame for payload, header included, byte for byte what WriteFrame
// sends.
[[nodiscard]] std::string EncodeFrame(std::string_view payload, bool compress);

} // namespace ned::editor::protocol

#endif // NED_EDITOR_PROTOCOL_FRAMETRANSPORT_H
