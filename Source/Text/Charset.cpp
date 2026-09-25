#include "Charset.h"

#include <mutex>
#include <utility>

namespace ned::text {

namespace {

    constexpr std::string_view kUtf8Bom    = "\xEF\xBB\xBF";
    constexpr std::string_view kUtf16LeBom = "\xFF\xFE";
    constexpr std::string_view kUtf16BeBom = "\xFE\xFF";

    struct StatedResolverState {
        std::mutex                                                          mutex;
        std::function<std::optional<Charset>(const std::filesystem::path&)> resolver;
    };

    StatedResolverState& StatedResolver() {
        static StatedResolverState state;
        return state;
    }

    void AppendUtf8(char32_t codepoint, std::string& out) {
        if (codepoint < 0x80) {
            out.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else if (codepoint < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else {
            out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    void AppendUtf16Unit(std::uint16_t unit, bool bigEndian, std::string& out) {
        const auto high = static_cast<char>(unit >> 8);
        const auto low  = static_cast<char>(unit & 0xFF);
        if (bigEndian) {
            out.push_back(high);
            out.push_back(low);
        }
        else {
            out.push_back(low);
            out.push_back(high);
        }
    }

    // Length of the UTF-8 sequence a lead byte starts; 0 for a byte that
    // can't start one.
    std::size_t SequenceLength(unsigned char lead) {
        if (lead < 0x80) {
            return 1;
        }
        if (lead >= 0xC2 && lead <= 0xDF) {
            return 2;
        }
        if (lead >= 0xE0 && lead <= 0xEF) {
            return 3;
        }
        if (lead >= 0xF0 && lead <= 0xF4) {
            return 4;
        }
        return 0;
    }

    // The codepoint a complete sequence of SequenceLength(bytes[0]) bytes
    // spells, or nullopt for a malformed, overlong or surrogate one.
    std::optional<char32_t> DecodeSequence(std::string_view bytes) {
        const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
        for (std::size_t i = 1; i < bytes.size(); ++i) {
            if ((byte(i) & 0xC0) != 0x80) {
                return std::nullopt;
            }
        }
        char32_t codepoint = 0;
        switch (bytes.size()) {
            case 1:
                return byte(0);
            case 2:
                return static_cast<char32_t>(((byte(0) & 0x1F) << 6) | (byte(1) & 0x3F));
            case 3:
                codepoint = static_cast<char32_t>(((byte(0) & 0x0F) << 12) | ((byte(1) & 0x3F) << 6) | (byte(2) & 0x3F));
                if (codepoint < 0x800 || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
                    return std::nullopt;
                }
                return codepoint;
            case 4:
                codepoint = static_cast<char32_t>(((byte(0) & 0x07) << 18) | ((byte(1) & 0x3F) << 12) | ((byte(2) & 0x3F) << 6) |
                                                  (byte(3) & 0x3F));
                if (codepoint < 0x10000 || codepoint > 0x10FFFF) {
                    return std::nullopt;
                }
                return codepoint;
            default:
                return std::nullopt;
        }
    }

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
    for (const Charset charset : {Charset::Utf8, Charset::Utf8Bom, Charset::Latin1, Charset::Utf16Le, Charset::Utf16Be}) {
        if (name == CharsetName(charset)) {
            return charset;
        }
    }
    return std::nullopt;
}

std::string_view CharsetName(Charset charset) {
    switch (charset) {
        case Charset::Utf8:
            return "utf-8";
        case Charset::Utf8Bom:
            return "utf-8-bom";
        case Charset::Latin1:
            return "latin1";
        case Charset::Utf16Le:
            return "utf-16le";
        case Charset::Utf16Be:
            return "utf-16be";
    }
    return "utf-8";
}

bool IsUtf8Family(Charset charset) {
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

Charset ResolveLoadCharset(const std::filesystem::path& path, std::string_view head, std::optional<Charset> chosen) {
    const Charset sniffed = SniffCharset(head);
    if (!chosen && sniffed == Charset::Utf8) {
        std::function<std::optional<Charset>(const std::filesystem::path&)> resolver;
        {
            std::scoped_lock lock(StatedResolver().mutex);
            resolver = StatedResolver().resolver;
        }
        if (resolver) {
            chosen = resolver(path);
        }
    }
    if (!chosen || IsUtf8Family(*chosen)) {
        return (chosen && sniffed != Charset::Utf8Bom) ? Charset::Utf8 : sniffed;
    }
    return *chosen;
}

std::size_t PreambleLength(std::string_view head, Charset charset) {
    const std::string_view preamble = CharsetPreamble(charset);
    return head.starts_with(preamble) ? preamble.size() : 0;
}

void SetStatedCharsetResolver(std::function<std::optional<Charset>(const std::filesystem::path&)> resolver) {
    std::scoped_lock lock(StatedResolver().mutex);
    StatedResolver().resolver = std::move(resolver);
}

CharsetDecoder::CharsetDecoder(Charset charset) : charset_(charset) {
}

bool CharsetDecoder::Feed(std::string_view bytes, std::string& out) {
    if (failed_) {
        return false;
    }
    if (IsUtf8Family(charset_)) {
        out.append(bytes);
        return true;
    }
    if (charset_ == Charset::Latin1) {
        out.reserve(out.size() + bytes.size());
        for (const char c : bytes) {
            AppendUtf8(static_cast<unsigned char>(c), out);
        }
        return true;
    }

    const bool bigEndian = charset_ == Charset::Utf16Be;
    const auto unitAt    = [&](std::string_view units, std::size_t i) {
        const auto first  = static_cast<unsigned char>(units[i]);
        const auto second = static_cast<unsigned char>(units[i + 1]);
        return static_cast<std::uint16_t>(bigEndian ? (first << 8) | second : (second << 8) | first);
    };

    std::string pending = std::move(carry_);
    pending.append(bytes);
    carry_.clear();
    out.reserve(out.size() + pending.size());

    std::size_t i = 0;
    while (i + 2 <= pending.size()) {
        const std::uint16_t unit = unitAt(pending, i);
        if (unit >= 0xDC00 && unit <= 0xDFFF) {
            failed_ = true;
            return false;
        }
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (i + 4 > pending.size()) {
                break; // its low half hasn't arrived yet
            }
            const std::uint16_t low = unitAt(pending, i + 2);
            if (low < 0xDC00 || low > 0xDFFF) {
                failed_ = true;
                return false;
            }
            AppendUtf8(0x10000 + ((static_cast<char32_t>(unit - 0xD800) << 10) | (low - 0xDC00)), out);
            i += 4;
            continue;
        }
        AppendUtf8(unit, out);
        i += 2;
    }
    carry_.assign(pending, i);
    return true;
}

bool CharsetDecoder::Finish() const {
    return !failed_ && carry_.empty();
}

std::optional<std::string> DecodeCharset(std::string_view bytes, Charset charset) {
    CharsetDecoder decoder(charset);
    std::string    out;
    if (!decoder.Feed(bytes, out) || !decoder.Finish()) {
        return std::nullopt;
    }
    return out;
}

std::optional<std::string> DecodeAnnouncedCharset(std::string bytes, std::optional<Charset> stated) {
    const Charset sniffed = SniffCharset(bytes);
    const Charset charset = (sniffed == Charset::Utf8 && stated) ? *stated : sniffed;
    if (IsUtf8Family(charset)) {
        bytes.erase(0, PreambleLength(bytes, charset));
        return bytes;
    }
    return DecodeCharset(std::string_view(bytes).substr(PreambleLength(bytes, charset)), charset);
}

CharsetEncoder::CharsetEncoder(Charset charset) : charset_(charset) {
}

bool CharsetEncoder::Feed(std::string_view utf8, std::string& out) {
    if (failed_) {
        return false;
    }
    if (IsUtf8Family(charset_)) {
        out.append(utf8);
        consumed_ += utf8.size();
        return true;
    }

    // Offsets below count from the start of `pending`, which begins with
    // the carried bytes already counted into consumed_.
    const std::size_t base    = consumed_ - carry_.size();
    std::string       pending = std::move(carry_);
    pending.append(utf8);
    carry_.clear();
    consumed_ += utf8.size();

    std::size_t i = 0;
    while (i < pending.size()) {
        const std::size_t length = SequenceLength(static_cast<unsigned char>(pending[i]));
        if (length == 0) {
            failed_   = true;
            consumed_ = base + i;
            return false;
        }
        if (i + length > pending.size()) {
            carry_.assign(pending, i);
            return true;
        }
        const std::optional<char32_t> codepoint = DecodeSequence(std::string_view(pending).substr(i, length));
        if (!codepoint || (charset_ == Charset::Latin1 && *codepoint > 0xFF)) {
            failed_   = true;
            consumed_ = base + i;
            return false;
        }
        if (charset_ == Charset::Latin1) {
            out.push_back(static_cast<char>(*codepoint));
        }
        else {
            const bool bigEndian = charset_ == Charset::Utf16Be;
            if (*codepoint >= 0x10000) {
                const char32_t offset = *codepoint - 0x10000;
                AppendUtf16Unit(static_cast<std::uint16_t>(0xD800 + (offset >> 10)), bigEndian, out);
                AppendUtf16Unit(static_cast<std::uint16_t>(0xDC00 + (offset & 0x3FF)), bigEndian, out);
            }
            else {
                AppendUtf16Unit(static_cast<std::uint16_t>(*codepoint), bigEndian, out);
            }
        }
        i += length;
    }
    return true;
}

bool CharsetEncoder::Finish() const {
    return !failed_ && carry_.empty();
}

std::size_t CharsetEncoder::FailedAt() const {
    return failed_ ? consumed_ : consumed_ - carry_.size();
}

std::optional<std::string> EncodeCharset(std::string_view utf8, Charset charset) {
    CharsetEncoder encoder(charset);
    std::string    out;
    if (!encoder.Feed(utf8, out) || !encoder.Finish()) {
        return std::nullopt;
    }
    return out;
}

} // namespace ned::text
