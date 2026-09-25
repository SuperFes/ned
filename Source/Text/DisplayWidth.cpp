#include "DisplayWidth.h"

#include <algorithm>
#include <cstdlib>

#include <notcurses/notcurses.h>
#include <utf8proc.h>

#include "ITextStorage.h"
#include "Utf8.h"

namespace ned::text {

namespace {

    constexpr char32_t kPlaceholderOpen  = U'◁';
    constexpr char32_t kPlaceholderClose = U'▷';
    constexpr int      kRawByteColumns   = 6; // ◁\xE9▷

    bool IsControl(char32_t cp) {
        return (cp < 0x20 && cp != U'\t') || (cp >= 0x7F && cp <= 0x9F);
    }

    bool IsAsciiPrintable(char32_t cp) {
        return cp >= 0x20 && cp < 0x7F;
    }

    Glyph PlaceholderGlyph(char32_t cp, std::size_t byteLength) {
        return Glyph{.byteLength = byteLength, .codepoint = cp, .columns = PlaceholderColumns(cp), .placeholder = true};
    }

    Glyph RawByteGlyph(char32_t byte) {
        return Glyph{.byteLength = 1, .codepoint = byte, .columns = kRawByteColumns, .placeholder = true, .rawByte = true};
    }

    // One decoded step: a codepoint, or a byte that isn't part of well-formed
    // UTF-8 (codepoint holds the byte's value).
    struct Decoded {
        char32_t    codepoint = 0;
        std::size_t length    = 0;
        bool        raw       = false;
    };

    // Measures an already-segmented, well-formed cluster.
    Glyph Measure(const std::string& cluster, char32_t first, std::size_t byteLength) {
        const int columns = ncstrwidth(cluster.c_str(), nullptr, nullptr);
        if (columns > 0) {
            return Glyph{.byteLength = byteLength, .codepoint = first, .columns = std::min(columns, 2)};
        }
        // Notcurses can't measure anything but ASCII outside a UTF-8 locale;
        // count one column rather than hiding every other character.
        if (MB_CUR_MAX == 1) {
            return Glyph{.byteLength = byteLength, .codepoint = first, .columns = 1};
        }
        return PlaceholderGlyph(first, byteLength);
    }

    // The shared segmentation: `decode(offset)` yields a Decoded.
    template <typename Decode>
    Glyph ClusterAt(std::size_t offset, std::size_t end, Decode decode) {
        const Decoded first = decode(offset);
        if (first.raw) {
            return RawByteGlyph(first.codepoint);
        }
        if (first.codepoint == U'\t') {
            return Glyph{.byteLength = first.length, .codepoint = first.codepoint, .columns = 1};
        }
        if (IsControl(first.codepoint)) {
            return PlaceholderGlyph(first.codepoint, first.length);
        }

        std::size_t next = offset + first.length;
        if (IsAsciiPrintable(first.codepoint)) {
            // ASCII is its own cluster unless something combines with it.
            if (next >= end || decode(next).codepoint < 0x80) {
                return Glyph{.byteLength = first.length, .codepoint = first.codepoint, .columns = 1};
            }
        }

        std::string      cluster = EncodeCodepointUtf8(first.codepoint);
        char32_t         prev    = first.codepoint;
        utf8proc_int32_t state   = 0;
        while (next < end) {
            const Decoded step = decode(next);
            if (step.raw || IsControl(step.codepoint) || step.codepoint == U'\t' ||
                utf8proc_grapheme_break_stateful(static_cast<utf8proc_int32_t>(prev),
                                                 static_cast<utf8proc_int32_t>(step.codepoint), &state)) {
                break;
            }
            cluster += EncodeCodepointUtf8(step.codepoint);
            prev = step.codepoint;
            next += step.length;
        }
        return Measure(cluster, first.codepoint, next - offset);
    }

    template <typename Decode>
    std::string ClusterText(std::size_t offset, const Glyph& glyph, Decode decode) {
        if (glyph.placeholder) {
            return PlaceholderText(glyph);
        }
        std::string text;
        for (std::size_t at = offset; at < offset + glyph.byteLength;) {
            const Decoded step = decode(at);
            text += EncodeCodepointUtf8(step.codepoint);
            at += step.length;
        }
        return text;
    }

    // The length a lead byte announces; only asked of a sequence that decoded.
    std::size_t SequenceLength(unsigned char lead) {
        if (lead < 0x80) {
            return 1;
        }
        if ((lead & 0xE0) == 0xC0) {
            return 2;
        }
        return (lead & 0xF0) == 0xE0 ? 3 : 4;
    }

    // Both decoders step over a malformed byte one byte at a time, which is
    // how the storage already decodes it ({U+FFFD, 1}); a real U+FFFD is
    // three bytes long.
    auto StorageDecoder(const ITextStorage& content) {
        return [&content](std::size_t at) {
            const auto decoded = content.CodepointAt(at);
            if (decoded.codepoint == 0xFFFD && decoded.byteLength == 1) {
                return Decoded{.codepoint = static_cast<unsigned char>(content.Substring(at, 1)[0]), .length = 1, .raw = true};
            }
            return Decoded{.codepoint = decoded.codepoint, .length = decoded.byteLength};
        };
    }

    auto StringDecoder(std::string_view text) {
        return [text](std::size_t at) {
            const char32_t cp = DecodeCodepointUtf8(text, at);
            if (cp == 0xFFFD && text.substr(at, 3) != "\xEF\xBF\xBD") {
                return Decoded{.codepoint = static_cast<unsigned char>(text[at]), .length = 1, .raw = true};
            }
            return Decoded{.codepoint = cp, .length = SequenceLength(static_cast<unsigned char>(text[at]))};
        };
    }

} // namespace

Glyph GlyphAt(const ITextStorage& content, std::size_t offset, std::size_t end) {
    return ClusterAt(offset, std::min(end, content.ByteLength()), StorageDecoder(content));
}

Glyph GlyphAt(std::string_view text, std::size_t offset) {
    return ClusterAt(offset, text.size(), StringDecoder(text));
}

int PlaceholderDigits(char32_t codepoint) {
    if (codepoint <= 0xFF) {
        return 2;
    }
    return codepoint <= 0xFFFF ? 4 : 6;
}

std::string PlaceholderText(char32_t codepoint) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string           text   = EncodeCodepointUtf8(kPlaceholderOpen);
    for (int digit = PlaceholderDigits(codepoint) - 1; digit >= 0; --digit) {
        text += kHex[(codepoint >> (digit * 4)) & 0xF];
    }
    text += EncodeCodepointUtf8(kPlaceholderClose);
    return text;
}

std::string PlaceholderText(const Glyph& glyph) {
    if (!glyph.rawByte) {
        return PlaceholderText(glyph.codepoint);
    }
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string           text   = EncodeCodepointUtf8(kPlaceholderOpen);
    text += "\\x";
    text += kHex[(glyph.codepoint >> 4) & 0xF];
    text += kHex[glyph.codepoint & 0xF];
    text += EncodeCodepointUtf8(kPlaceholderClose);
    return text;
}

std::string GlyphText(const ITextStorage& content, std::size_t offset, const Glyph& glyph) {
    return ClusterText(offset, glyph, StorageDecoder(content));
}

std::string GlyphText(std::string_view text, std::size_t offset, const Glyph& glyph) {
    return ClusterText(offset, glyph, StringDecoder(text));
}

int StringColumns(std::string_view text, int startColumn, int tabWidth) {
    int column = startColumn;
    for (std::size_t offset = 0; offset < text.size();) {
        const Glyph glyph = GlyphAt(text, offset);
        column += GlyphColumns(glyph, column, tabWidth);
        offset += glyph.byteLength;
    }
    return column - startColumn;
}

std::size_t PrefixBytesForColumns(std::string_view text, int columns) {
    int         used   = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const Glyph glyph = GlyphAt(text, offset);
        if (used + glyph.columns > columns) {
            break;
        }
        used += glyph.columns;
        offset += glyph.byteLength;
    }
    return offset;
}

} // namespace ned::text
