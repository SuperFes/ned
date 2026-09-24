#include "Charset.h"

namespace ned::text {

namespace {

    constexpr std::string_view kUtf8Bom    = "\xEF\xBB\xBF";
    constexpr std::string_view kUtf16LeBom = "\xFF\xFE";
    constexpr std::string_view kUtf16BeBom = "\xFE\xFF";

} // namespace

Charset SniffCharset(std::string_view head) {
    if (head.starts_with(kUtf8Bom)) {
        return Charset::Utf8Bom;
    }
    if (head.starts_with(kUtf16LeBom)) {
        return Charset::Utf16Le;
    }
    if (head.starts_with(kUtf16BeBom)) {
        return Charset::Utf16Be;
    }
    return Charset::Utf8;
}

std::optional<Charset> CharsetFromName(std::string_view name) {
    if (name == "utf-8") {
        return Charset::Utf8;
    }
    if (name == "utf-8-bom") {
        return Charset::Utf8Bom;
    }
    if (name == "latin1") {
        return Charset::Latin1;
    }
    if (name == "utf-16le") {
        return Charset::Utf16Le;
    }
    if (name == "utf-16be") {
        return Charset::Utf16Be;
    }
    return std::nullopt;
}

bool CharsetConverts(Charset charset) {
    return charset == Charset::Utf8 || charset == Charset::Utf8Bom;
}

std::string_view CharsetPreamble(Charset charset) {
    switch (charset) {
        case Charset::Utf8Bom:
            return kUtf8Bom;
        case Charset::Utf16Le:
            return kUtf16LeBom;
        case Charset::Utf16Be:
            return kUtf16BeBom;
        case Charset::Utf8:
        case Charset::Latin1:
            return {};
    }
    return {};
}

} // namespace ned::text
