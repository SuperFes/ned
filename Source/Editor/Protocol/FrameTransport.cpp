#include "FrameTransport.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <unistd.h>
#include <zstd.h>

namespace ned::editor::protocol {

namespace {

    // A middle level: most of zstd's ratio at a fraction of the high levels'
    // CPU, which matters more than ratio on a link that's keeping up.
    constexpr int kZstdLevel = 3;

    std::array<char, kFrameHeaderBytes> Header(std::size_t length, unsigned char flags) {
        const auto value = static_cast<std::uint32_t>(length);
        return {static_cast<char>(value >> 24), static_cast<char>(value >> 16), static_cast<char>(value >> 8),
                static_cast<char>(value), static_cast<char>(flags)};
    }

    std::string Compress(std::string_view payload) {
        std::string       out(ZSTD_compressBound(payload.size()), '\0');
        const std::size_t size = ZSTD_compress(out.data(), out.size(), payload.data(), payload.size(), kZstdLevel);
        if (ZSTD_isError(size) != 0U) {
            throw std::runtime_error(std::string("ned: frame compression failed: ") + ZSTD_getErrorName(size));
        }
        out.resize(size);
        return out;
    }

    // The compressed body, or empty when payload goes out as-is. Throws if
    // payload is over the cap either way.
    std::string CompressIfWorthIt(std::string_view payload, bool compress) {
        if (payload.size() > kMaxFrameBytes) {
            throw std::runtime_error("ned: frame payload exceeds the size cap");
        }
        return compress && payload.size() >= kCompressMinBytes ? Compress(payload) : std::string();
    }

    std::string Decompress(std::string_view payload) {
        // ZSTD_compress always records the content size; a frame without one
        // didn't come from a peer of ours, and would leave nothing to cap.
        const unsigned long long size = ZSTD_getFrameContentSize(payload.data(), payload.size());
        if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN) {
            throw std::runtime_error("ned: compressed frame has no readable content size");
        }
        if (size > kMaxFrameBytes) {
            throw std::runtime_error("ned: compressed frame decompresses past the size cap");
        }
        std::string       out(static_cast<std::size_t>(size), '\0');
        const std::size_t got = ZSTD_decompress(out.data(), out.size(), payload.data(), payload.size());
        if (ZSTD_isError(got) != 0U || got != out.size()) {
            throw std::runtime_error("ned: compressed frame is corrupt");
        }
        return out;
    }

    // Reads exactly n bytes. Returns false on EOF before the first byte when
    // atFrameStart (a clean disconnect); EOF anywhere else throws. Only that
    // first byte waits unbounded.
    bool ReadExact(const process::ChildProcess& child, char* buffer, std::size_t n, bool atFrameStart,
                   std::chrono::milliseconds stallTimeout) {
        std::size_t got = 0;
        while (got < n) {
            const bool waitUnbounded = atFrameStart && got == 0;
            if (!child.WaitReadable(waitUnbounded ? std::chrono::milliseconds(-1) : stallTimeout)) {
                if (waitUnbounded) {
                    return false; // an unbounded wait only fails once the connection is closed
                }
                throw std::runtime_error("ned: frame transport stalled mid-frame");
            }
            const ssize_t result = ::read(child.ReadFd(), buffer + got, n - got);
            if (result < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                throw std::runtime_error(std::string("ned: frame transport read failed: ") + std::strerror(errno));
            }
            if (result == 0) {
                if (atFrameStart && got == 0) {
                    return false;
                }
                throw std::runtime_error("ned: frame transport EOF mid-frame");
            }
            got += static_cast<std::size_t>(result);
        }
        return true;
    }

    struct FrameHeader {
        std::size_t   length = 0;
        unsigned char flags  = 0;
    };

    FrameHeader ParseHeader(std::string_view header) {
        const auto        byte   = [header](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>(header[i])); };
        const FrameHeader parsed = {.length = (byte(0) << 24) | (byte(1) << 16) | (byte(2) << 8) | byte(3), .flags = static_cast<unsigned char>(header[4])};
        if ((parsed.flags & ~kFrameFlagZstd) != 0) {
            throw std::runtime_error("ned: frame has a reserved flag set");
        }
        if (parsed.length > kMaxFrameBytes) {
            throw std::runtime_error("ned: frame length exceeds the size cap");
        }
        return parsed;
    }

    std::string BaseName(const std::string& path) {
        const std::size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

} // namespace

std::string EncodeFrame(std::string_view payload, bool compress) {
    const std::string      compressed = CompressIfWorthIt(payload, compress);
    const std::string_view body       = compressed.empty() ? payload : std::string_view(compressed);
    const auto             header     = Header(body.size(), compressed.empty() ? 0 : kFrameFlagZstd);
    std::string            frame(header.begin(), header.end());
    frame += body;
    return frame;
}

std::vector<std::string> TakeFrames(std::string& bytes) {
    std::vector<std::string> frames;
    std::size_t              at = 0;
    while (bytes.size() - at >= kFrameHeaderBytes) {
        const FrameHeader header = ParseHeader(std::string_view(bytes).substr(at, kFrameHeaderBytes));
        if (bytes.size() - at - kFrameHeaderBytes < header.length) {
            break;
        }
        const std::string_view payload = std::string_view(bytes).substr(at + kFrameHeaderBytes, header.length);
        frames.push_back((header.flags & kFrameFlagZstd) != 0 ? Decompress(payload) : std::string(payload));
        at += kFrameHeaderBytes + header.length;
    }
    bytes.erase(0, at);
    return frames;
}

FrameTransport::FrameTransport(const std::vector<std::string>& argv, bool captureStderr) : child_(argv, captureStderr ? process::StderrMode::Capture : process::StderrMode::Discard),
                                                                                           processLabel_(argv.empty() ? std::string() : BaseName(argv[0])) {
}

FrameTransport::FrameTransport(int readFd, int writeFd, pid_t pid) : child_(readFd, writeFd, pid) {
}

void FrameTransport::WriteFrame(std::string_view payload, std::chrono::milliseconds stallTimeout) const {
    const std::string      compressed = CompressIfWorthIt(payload, compress_->load());
    const std::string_view body       = compressed.empty() ? payload : std::string_view(compressed);
    const auto             header     = Header(body.size(), compressed.empty() ? 0 : kFrameFlagZstd);

    // Header and body must reach the peer as one run of bytes.
    const std::lock_guard<std::mutex> writeLock(*writeMutex_);
    child_.WriteAll(std::string_view(header.data(), header.size()), stallTimeout);
    child_.WriteAll(body, stallTimeout);
}

std::optional<std::string> FrameTransport::ReadFrame(std::chrono::milliseconds stallTimeout) const {
    std::array<char, kFrameHeaderBytes> header{};
    if (!ReadExact(child_, header.data(), header.size(), true, stallTimeout)) {
        return std::nullopt;
    }
    const FrameHeader parsed = ParseHeader(std::string_view(header.data(), header.size()));

    std::string payload(parsed.length, '\0');
    if (parsed.length > 0) {
        ReadExact(child_, payload.data(), parsed.length, false, stallTimeout);
    }
    return (parsed.flags & kFrameFlagZstd) != 0 ? Decompress(payload) : payload;
}

bool FrameTransport::WaitReadable(std::chrono::milliseconds timeout) const {
    return child_.WaitReadable(timeout);
}

std::string FrameTransport::ReadUnframed() const {
    return child_.ReadSome();
}

void FrameTransport::Close() noexcept {
    child_.CloseConnection();
}

pid_t FrameTransport::Pid() const noexcept {
    return child_.Pid();
}

int FrameTransport::StderrFd() const noexcept {
    return child_.StderrFd();
}

std::string FrameTransport::ReadStderr() const {
    return child_.ReadSomeStderr();
}

const std::string& FrameTransport::ProcessLabel() const noexcept {
    return processLabel_;
}

} // namespace ned::editor::protocol
