#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Text/Charset.h"

using ned::text::Charset;
using ned::text::CharsetDecoder;
using ned::text::CharsetEncoder;
using ned::text::DecodeCharset;
using ned::text::EncodeCharset;

namespace {

std::string Bytes(std::initializer_list<unsigned char> bytes) {
    return {bytes.begin(), bytes.end()};
}

// A resolver installed for one test and removed after it, whatever happens.
struct StatedCharset {
    explicit StatedCharset(std::optional<Charset> stated) {
        ned::text::SetStatedCharsetResolver([stated](const std::filesystem::path&) { return stated; });
    }
    ~StatedCharset() {
        ned::text::SetStatedCharsetResolver(nullptr);
    }
};

} // namespace

TEST_CASE("Charset names round-trip through .editorconfig's spelling", "[Charset]") {
    for (const Charset charset : {Charset::Utf8, Charset::Utf8Bom, Charset::Latin1, Charset::Utf16Le, Charset::Utf16Be}) {
        CHECK(ned::text::CharsetFromName(ned::text::CharsetName(charset)) == charset);
    }
    CHECK_FALSE(ned::text::CharsetFromName("ebcdic").has_value());
}

TEST_CASE("Latin-1 decodes every byte and encodes back to the same bytes", "[Charset]") {
    std::string all;
    for (int b = 0; b < 256; ++b) {
        all.push_back(static_cast<char>(b));
    }
    const std::optional<std::string> text = DecodeCharset(all, Charset::Latin1);
    REQUIRE(text);
    CHECK(DecodeCharset("caf\xE9", Charset::Latin1) == "caf\xC3\xA9");
    CHECK(EncodeCharset(*text, Charset::Latin1) == all);
}

TEST_CASE("Latin-1 refuses a character it can't hold, and says where", "[Charset]") {
    CharsetEncoder encoder(Charset::Latin1);
    std::string    out;
    CHECK(encoder.Feed("ok \xC3\xA9 ", out));
    CHECK_FALSE(encoder.Feed("x\xE2\x9C\x93y", out)); // U+2713
    CHECK(encoder.FailedAt() == 7);
    CHECK(out == "ok \xE9 x");
    CHECK_FALSE(encoder.Finish());
}

TEST_CASE("UTF-16 decodes both byte orders, surrogate pairs included", "[Charset]") {
    // "aé😀"
    const std::string utf8 = "a\xC3\xA9\xF0\x9F\x98\x80";
    const std::string le   = Bytes({'a', 0, 0xE9, 0, 0x3D, 0xD8, 0x00, 0xDE});
    const std::string be   = Bytes({0, 'a', 0, 0xE9, 0xD8, 0x3D, 0xDE, 0x00});
    CHECK(DecodeCharset(le, Charset::Utf16Le) == utf8);
    CHECK(DecodeCharset(be, Charset::Utf16Be) == utf8);
    CHECK(EncodeCharset(utf8, Charset::Utf16Le) == le);
    CHECK(EncodeCharset(utf8, Charset::Utf16Be) == be);
}

TEST_CASE("UTF-16 that doesn't decode cleanly is refused, not patched", "[Charset]") {
    CHECK_FALSE(DecodeCharset(Bytes({'a', 0, 'b'}), Charset::Utf16Le));        // odd length
    CHECK_FALSE(DecodeCharset(Bytes({0x00, 0xDC, 'a', 0}), Charset::Utf16Le)); // lone low surrogate
    CHECK_FALSE(DecodeCharset(Bytes({0x3D, 0xD8, 'a', 0}), Charset::Utf16Le)); // high without low
    CHECK_FALSE(DecodeCharset(Bytes({0x3D, 0xD8}), Charset::Utf16Le));         // high at the end
}

TEST_CASE("The decoder carries a unit or pair split across reads", "[Charset]") {
    const std::string le = Bytes({'a', 0, 0x3D, 0xD8, 0x00, 0xDE, 'b', 0});
    for (std::size_t split = 0; split <= le.size(); ++split) {
        CharsetDecoder decoder(Charset::Utf16Le);
        std::string    out;
        REQUIRE(decoder.Feed(std::string_view(le).substr(0, split), out));
        REQUIRE(decoder.Feed(std::string_view(le).substr(split), out));
        CHECK(decoder.Finish());
        CHECK(out == "a\xF0\x9F\x98\x80"
                     "b");
    }
}

TEST_CASE("The encoder carries a UTF-8 sequence split across writes", "[Charset]") {
    const std::string utf8 = "\xC3\xA9\xF0\x9F\x98\x80";
    for (std::size_t split = 0; split <= utf8.size(); ++split) {
        CharsetEncoder encoder(Charset::Utf16Be);
        std::string    out;
        REQUIRE(encoder.Feed(std::string_view(utf8).substr(0, split), out));
        REQUIRE(encoder.Feed(std::string_view(utf8).substr(split), out));
        CHECK(encoder.Finish());
        CHECK(out == Bytes({0, 0xE9, 0xD8, 0x3D, 0xDE, 0x00}));
    }
}

TEST_CASE("Bytes that aren't UTF-8 pass through the UTF-8 family and nothing else", "[Charset]") {
    const std::string raw = "caf\xE9";
    CHECK(DecodeCharset(raw, Charset::Utf8) == raw);
    CHECK(EncodeCharset(raw, Charset::Utf8Bom) == raw);
    CHECK_FALSE(EncodeCharset(raw, Charset::Latin1));
    CHECK_FALSE(EncodeCharset(raw, Charset::Utf16Le));
    CHECK_FALSE(EncodeCharset("\xED\xA0\x80", Charset::Utf16Le)); // an encoded surrogate
    CHECK_FALSE(EncodeCharset("\xC0\xAF", Charset::Latin1));      // overlong

    CharsetEncoder truncated(Charset::Latin1);
    std::string    out;
    CHECK(truncated.Feed("a\xC3", out));
    CHECK_FALSE(truncated.Finish());
    CHECK(truncated.FailedAt() == 1);
}

TEST_CASE("A load's charset: chosen, then announced, then stated, then UTF-8", "[Charset]") {
    const std::filesystem::path path  = "f.txt";
    const std::string           le    = Bytes({0xFF, 0xFE, 'a', 0});
    const std::string           u8bom = "\xEF\xBB\xBFx";
    using ned::text::ResolveLoadCharset;

    CHECK(ResolveLoadCharset(path, "plain", std::nullopt) == Charset::Utf8);
    CHECK(ResolveLoadCharset(path, le, std::nullopt) == Charset::Utf16Le);
    CHECK(ResolveLoadCharset(path, le, Charset::Latin1) == Charset::Latin1);
    // Within the UTF-8 family the file's own BOM decides.
    CHECK(ResolveLoadCharset(path, u8bom, Charset::Utf8) == Charset::Utf8Bom);
    CHECK(ResolveLoadCharset(path, "plain", Charset::Utf8Bom) == Charset::Utf8);

    const StatedCharset stated(Charset::Latin1);
    CHECK(ResolveLoadCharset(path, "caf\xE9", std::nullopt) == Charset::Latin1);
    CHECK(ResolveLoadCharset(path, le, std::nullopt) == Charset::Utf16Le); // a BOM beats a statement
    CHECK(ResolveLoadCharset(path, "caf\xE9", Charset::Utf8) == Charset::Utf8);
}

TEST_CASE("A preamble is stripped only when the file actually starts with it", "[Charset]") {
    CHECK(ned::text::PreambleLength(Bytes({0xFF, 0xFE, 'a', 0}), Charset::Utf16Le) == 2);
    CHECK(ned::text::PreambleLength(Bytes({'a', 0}), Charset::Utf16Le) == 0);
    CHECK(ned::text::PreambleLength("abc", Charset::Latin1) == 0);
}

TEST_CASE("A whole-file decode follows a stated charset unless a byte order mark says otherwise", "[Charset]") {
    using ned::text::DecodeAnnouncedCharset;
    CHECK(DecodeAnnouncedCharset("caf\xE9", Charset::Latin1) == "caf\xC3\xA9");
    CHECK(DecodeAnnouncedCharset("caf\xE9") == "caf\xE9"); // nothing stated: bytes kept as read
    CHECK(DecodeAnnouncedCharset("\xEF\xBB\xBF" "caf\xC3\xA9", Charset::Latin1) == "caf\xC3\xA9");
    CHECK(DecodeAnnouncedCharset(std::string("a\0b\0", 4), Charset::Utf16Le) == "ab");
}
