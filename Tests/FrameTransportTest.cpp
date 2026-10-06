#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>
#include <zstd.h>

#include "Editor/Protocol/Cbor.h"
#include "Editor/Protocol/FrameTransport.h"

using ned::editor::protocol::CborError;
using ned::editor::protocol::DecodeCbor;
using ned::editor::protocol::EncodeCbor;
using ned::editor::protocol::EncodeFrame;
using ned::editor::protocol::FrameTransport;
using ned::editor::protocol::kCompressMinBytes;
using ned::editor::protocol::kFrameFlagZstd;
using ned::editor::protocol::kMaxCborDepth;
using ned::editor::protocol::kMaxFrameBytes;
using Json = nlohmann::json;

namespace {

// A transport reading from a pipe whose write end the test drives by hand.
struct RawPipe {
    FrameTransport reader;
    int            writeFd;

    static RawPipe Create() {
        int fds[2];
        REQUIRE(::pipe(fds) == 0);
        int unused[2];
        REQUIRE(::pipe(unused) == 0);
        ::close(unused[0]);
        return RawPipe{FrameTransport(fds[0], unused[1]), fds[1]};
    }

    void Write(const std::string& bytes) const {
        REQUIRE(::write(writeFd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
    }

    void CloseWriter() {
        ::close(writeFd);
        writeFd = -1;
    }
};

struct TransportPair {
    FrameTransport a;
    FrameTransport b;

    static TransportPair Create() {
        int toB[2];
        int toA[2];
        REQUIRE(::pipe(toB) == 0);
        REQUIRE(::pipe(toA) == 0);
        return TransportPair{FrameTransport(toA[0], toB[1]), FrameTransport(toB[0], toA[1])};
    }
};

std::string Header(std::uint32_t length, unsigned char flags) {
    return {static_cast<char>(length >> 24), static_cast<char>(length >> 16), static_cast<char>(length >> 8),
            static_cast<char>(length), static_cast<char>(flags)};
}

} // namespace

TEST_CASE("EncodeFrame writes a big-endian length and a flags byte", "[Protocol][FrameTransport]") {
    REQUIRE(EncodeFrame("abc", false) == Header(3, 0) + "abc");
    // Too small to be worth compressing, even when asked.
    REQUIRE(EncodeFrame("abc", true) == Header(3, 0) + "abc");

    const std::string big(4 * kCompressMinBytes, 'x');
    const std::string frame = EncodeFrame(big, true);
    REQUIRE(static_cast<unsigned char>(frame[4]) == kFrameFlagZstd);
    REQUIRE(frame.size() < big.size());
}

TEST_CASE("FrameTransport round-trips frames, compressed or not", "[Protocol][FrameTransport]") {
    TransportPair pair = TransportPair::Create();

    std::string binary;
    for (int i = 0; i < 256; ++i) {
        binary += static_cast<char>(i);
    }
    pair.a.WriteFrame(binary);
    REQUIRE(pair.b.ReadFrame() == binary);

    // Larger than a pipe's buffer, so the write runs beside the read.
    pair.a.SetCompression(true);
    std::string large;
    for (int i = 0; i < 200000; ++i) {
        large += "line " + std::to_string(i % 97) + "\n";
    }
    std::jthread writer([&] { pair.a.WriteFrame(large); });
    REQUIRE(pair.b.ReadFrame() == large);
}

TEST_CASE("FrameTransport reads a clean EOF between frames as the end", "[Protocol][FrameTransport]") {
    RawPipe pipe = RawPipe::Create();
    pipe.Write(EncodeFrame("one", false));
    pipe.CloseWriter();

    REQUIRE(pipe.reader.ReadFrame() == std::string("one"));
    REQUIRE_FALSE(pipe.reader.ReadFrame().has_value());
}

TEST_CASE("FrameTransport rejects malformed frames", "[Protocol][FrameTransport]") {
    RawPipe pipe = RawPipe::Create();

    SECTION("EOF mid-frame") {
        pipe.Write(Header(10, 0) + "short");
        pipe.CloseWriter();
        REQUIRE_THROWS_AS(pipe.reader.ReadFrame(), std::runtime_error);
    }
    SECTION("EOF mid-header") {
        pipe.Write(Header(10, 0).substr(0, 2));
        pipe.CloseWriter();
        REQUIRE_THROWS_AS(pipe.reader.ReadFrame(), std::runtime_error);
    }
    SECTION("a reserved flag") {
        pipe.Write(Header(1, 0x02) + "x");
        REQUIRE_THROWS_AS(pipe.reader.ReadFrame(), std::runtime_error);
    }
    SECTION("a length over the cap, before reading the payload") {
        pipe.Write(Header(static_cast<std::uint32_t>(kMaxFrameBytes + 1), 0));
        REQUIRE_THROWS_AS(pipe.reader.ReadFrame(), std::runtime_error);
    }
    SECTION("a corrupt compressed payload") {
        pipe.Write(Header(4, kFrameFlagZstd) + "junk");
        REQUIRE_THROWS_AS(pipe.reader.ReadFrame(), std::runtime_error);
    }
    SECTION("a compressed payload that would decompress past the cap") {
        const std::string zeros(kMaxFrameBytes + 1, '\0');
        std::string       compressed(ZSTD_compressBound(zeros.size()), '\0');
        compressed.resize(ZSTD_compress(compressed.data(), compressed.size(), zeros.data(), zeros.size(), 1));
        REQUIRE(compressed.size() < 65536); // fits the pipe in one write
        pipe.Write(Header(static_cast<std::uint32_t>(compressed.size()), kFrameFlagZstd) + compressed);
        REQUIRE_THROWS_AS(pipe.reader.ReadFrame(), std::runtime_error);
    }
    SECTION("a stall mid-frame") {
        pipe.Write(Header(10, 0) + "part");
        REQUIRE_THROWS_AS(pipe.reader.ReadFrame(std::chrono::milliseconds(50)), std::runtime_error);
    }
    if (pipe.writeFd >= 0) {
        pipe.CloseWriter();
    }
}

TEST_CASE("FrameTransport refuses to send a payload over the cap", "[Protocol][FrameTransport]") {
    REQUIRE_THROWS_AS(EncodeFrame(std::string(kMaxFrameBytes + 1, 'x'), false), std::runtime_error);
}

TEST_CASE("CBOR round-trips byte strings exactly", "[Protocol][Cbor]") {
    std::vector<std::uint8_t> bytes{0x00, 0xff, 0xc3, 0x28, '\n'}; // NUL and invalid UTF-8 included
    const Json                message{{"id", 7}, {"method", "fs/read"}, {"params", {{"data", Json::binary(bytes)}}}};

    const Json decoded = DecodeCbor(EncodeCbor(message));

    REQUIRE(decoded == message);
    REQUIRE(decoded["params"]["data"].is_binary());
    REQUIRE(static_cast<const std::vector<std::uint8_t>&>(decoded["params"]["data"].get_binary()) == bytes);
}

TEST_CASE("DecodeCbor rejects malformed and hostile input", "[Protocol][Cbor]") {
    const auto nested = [](std::size_t depth) {
        return std::string(depth, static_cast<char>(0x81)) + static_cast<char>(0xf6); // [[[...null...]]]
    };
    REQUIRE(DecodeCbor(nested(kMaxCborDepth)).is_array());
    REQUIRE_THROWS_AS(DecodeCbor(nested(kMaxCborDepth + 1)), CborError);
    REQUIRE_THROWS_AS(DecodeCbor(nested(1 << 20)), CborError);

    const std::string valid = EncodeCbor(Json{{"a", 1}});
    REQUIRE_THROWS_AS(DecodeCbor(valid + "x"), CborError);                            // trailing bytes
    REQUIRE_THROWS_AS(DecodeCbor(valid.substr(0, valid.size() - 1)), CborError);      // truncated
    REQUIRE_THROWS_AS(DecodeCbor(std::string("\xc0\x61x", 3)), CborError);            // a tag
    REQUIRE_THROWS_AS(DecodeCbor(std::string("\x9a\xff\xff\xff\xff", 5)), CborError); // a length past the input
}
