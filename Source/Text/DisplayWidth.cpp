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

    bool IsControl(char32_t cp) {
        return (cp < 0x20 && cp != U'\t') || (cp >= 0x7F && cp <= 0x9F);
    }

    bool IsAsciiPrintable(char32_t cp) {
        return cp >= 0x20 && cp < 0x7F;
    }

    Glyph PlaceholderGlyph(char32_t cp, std::size_t byteLength) {
        return Glyph{.byteLength = byteLength, .codepoint = cp, .columns = PlaceholderColumns(cp), .placeholder = true};
    }

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

    // The shared segmentation: `decode(offset)` yields {codepoint, byteLength}.
    template <typename Decode>
    Glyph ClusterAt(std::size_t offset, std::size_t end, Decode decode) {
        const auto [first, firstLength] = decode(offset);
        if (first == U'\t') {
            return Glyph{.byteLength = firstLength, .codepoint = first, .columns = 1};
        }
        if (IsControl(first)) {
            return PlaceholderGlyph(first, firstLength);
        }

        std::size_t next = offset + firstLength;
        if (IsAsciiPrintable(first)) {
            // ASCII is its own cluster unless something combines with it.
            if (next >= end || decode(next).first < 0x80) {
                return Glyph{.byteLength = firstLength, .codepoint = first, .columns = 1};
            }
        }

        std::string      cluster = EncodeCodepointUtf8(first);
        char32_t         prev    = first;
        utf8proc_int32_t state   = 0;
        while (next < end) {
            const auto [cp, length] = decode(next);
            if (IsControl(cp) || cp == U'\t' ||
                utf8proc_grapheme_break_stateful(static_cast<utf8proc_int32_t>(prev), static_cast<utf8proc_int32_t>(cp), &state)) {
                break;
            }
            cluster += EncodeCodepointUtf8(cp);
            prev = cp;
            next += length;
        }
        return Measure(cluster, first, next - offset);
    }

    template <typename Decode>
    std::string ClusterText(std::size_t offset, const Glyph& glyph, Decode decode) {
        if (glyph.placeholder) {
            return PlaceholderText(glyph.codepoint);
        }
        std::string text;
        for (std::size_t at = offset; at < offset + glyph.byteLength;) {
            const auto [cp, length] = decode(at);
            text += EncodeCodepointUtf8(cp);
            at += length;
        }
        return text;
    }

    auto StorageDecoder(const ITextStorage& content) {
        return [&content](std::size_t at) {
            const auto decoded = content.CodepointAt(at);
            return std::pair<char32_t, std::size_t>{decoded.codepoint, decoded.byteLength};
        };
    }

    auto StringDecoder(std::string_view text) {
        return [text](std::size_t at) {
            return std::pair<char32_t, std::size_t>{DecodeCodepointUtf8(text, at), NextCodepointBoundary(text, at) - at};
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
