//
// How a file's bytes are encoded on disk, as distinct from the buffer's own
// text, which is always UTF-8. The one seam every load and save passes a
// file's encoding through: loading sniffs it (SniffCharset) and strips its
// preamble, a save writes the preamble back ahead of the content.
//
// Only the UTF-8 family converts today (CharsetConverts). The others are
// named so .editorconfig can state them and a load can recognize them; a
// file in one is read and written byte for byte, unconverted. Converting
// one means a decode where the three load paths strip the preamble
// (Buffer::FromFile, Buffer::FromHugeFile, UI/AsyncFileLoader) and an encode
// in SavePlan's WritePlanContent -- nothing else in the editor sees a
// file's bytes.
//

#ifndef NED_TEXT_CHARSET_H
#define NED_TEXT_CHARSET_H

#include <cstdint>
#include <optional>
#include <string_view>

namespace ned::text {

enum class Charset : std::uint8_t { Utf8,
                                    Utf8Bom,
                                    Latin1,
                                    Utf16Le,
                                    Utf16Be };

// What a file's first bytes announce: a UTF-8 or UTF-16 byte order mark,
// else plain UTF-8. Latin-1 announces nothing, so it's never sniffed.
[[nodiscard]] Charset SniffCharset(std::string_view head);

// .editorconfig's spelling: utf-8, utf-8-bom, latin1, utf-16le, utf-16be.
[[nodiscard]] std::optional<Charset> CharsetFromName(std::string_view name);

// Whether text in `charset` converts to and from the buffer's UTF-8.
[[nodiscard]] bool CharsetConverts(Charset charset);

// The bytes a file in `charset` starts with (its byte order mark).
[[nodiscard]] std::string_view CharsetPreamble(Charset charset);

} // namespace ned::text

#endif // NED_TEXT_CHARSET_H
