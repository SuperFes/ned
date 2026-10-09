//
// ned --protocol-dump against captures built frame by frame.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Protocol/Cbor.h"
#include "Editor/Protocol/FrameTransport.h"
#include "Editor/Remote/ProtocolDump.h"

using Catch::Matchers::ContainsSubstring;
using ned::editor::protocol::EncodeCbor;
using ned::editor::protocol::EncodeFrame;
using ned::editor::remote::DumpProtocol;
using ned::editor::remote::ReadableMessage;
using Json = nlohmann::json;

namespace {

Json Bytes(const std::string& text) {
    return Json::binary(std::vector<std::uint8_t>(text.begin(), text.end()));
}

} // namespace

TEST_CASE("Byte strings print as text when they're UTF-8, else as their length and leading hex", "[Remote][ProtocolDump]") {
    const Json message{{"id", 3}, {"params", {{"path", Bytes("/srv/é.txt")}, {"chunks", Json::array({Bytes(std::string("\xff\x00", 2)), Bytes("ok")})}}}};
    CHECK(ReadableMessage(message) == Json{{"id", 3}, {"params", {{"path", "/srv/é.txt"}, {"chunks", Json::array({Json{{"bytes", 2}, {"hex", "ff00"}}, "ok"})}}}});
    // An overlong encoding and a lone surrogate aren't UTF-8.
    CHECK(ReadableMessage(Bytes("\xc0\xaf")).is_object());
    CHECK(ReadableMessage(Bytes("\xed\xa0\x80")).is_object());
    CHECK(ReadableMessage(Bytes(std::string(100, '\xfe')))["hex"].get<std::string>().size() == 64);
}

TEST_CASE("A capture prints one message per line, past an agent session's preamble", "[Remote][ProtocolDump]") {
    const Json         hello{{"id", 1}, {"result", {{"build", "abc"}}}};
    const Json         chunk{{"method", "$/chunk"}, {"params", {{"id", 2}, {"data", Bytes(std::string(4096, 'q'))}}}};
    const std::string  capture = "Welcome!\nned-agent-stdio-1 /home/u/.local/share/ned/agent/abc-x86_64/ned-agent\n" +
                                 EncodeFrame(EncodeCbor(hello), false) + EncodeFrame(EncodeCbor(chunk), true) + EncodeFrame("\xff", false);
    std::ostringstream out;
    std::ostringstream err;
    CHECK_FALSE(DumpProtocol(capture, out, err));
    CHECK(out.str() == hello.dump() + "\n" + ReadableMessage(chunk).dump() + "\n");
    CHECK_THAT(err.str(), ContainsSubstring("frame 3: "));
}

TEST_CASE("A capture cut off partway through a frame says so", "[Remote][ProtocolDump]") {
    const std::string  frame = EncodeFrame(EncodeCbor(Json{{"method", "x"}}), false);
    std::ostringstream out;
    std::ostringstream err;
    CHECK(DumpProtocol(frame, out, err));
    CHECK_FALSE(DumpProtocol(frame + frame.substr(0, 3), out, err));
    CHECK_THAT(err.str(), ContainsSubstring("partway through a frame (3 bytes left)"));
}
