//
// How a file's bytes are encoded on disk, as distinct from the buffer's own
// text, which is UTF-8. The one seam every load and save passes a file's
// encoding through: a load settles the charset (ResolveLoadCharset), strips
// its preamble and decodes the rest (CharsetDecoder); a save writes the
// preamble back and encodes (CharsetEncoder).
//
// Nothing is ever converted lossily. The UTF-8 family is not decoded at
// all, so bytes that aren't valid UTF-8 stay in the buffer exactly as read
// and are written back the same way; a UTF-16 file that doesn't decode
// cleanly is left to the binary guard; and a save whose charset can't
// represent some character is refused rather than substituted.
//

#ifndef NED_TEXT_CHARSET_H
#define NED_TEXT_CHARSET_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
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
[[nodiscard]] std::string_view       CharsetName(Charset charset);

// Whether the buffer holds a file in `charset` exactly as its bytes (after
// the preamble) -- nothing to decode or encode.
[[nodiscard]] bool IsUtf8Family(Charset charset);

// The bytes a file in `charset` starts with (its byte order mark). UTF-16
// is always written with one.
[[nodiscard]] std::string_view CharsetPreamble(Charset charset);

// The charset a load decodes with: an explicit choice (a command naming
// one), else what the file's own byte order mark announces, else what its
// surroundings state (the resolver below, which .editorconfig feeds), else
// UTF-8. Within the UTF-8 family the file's own BOM decides which one.
[[nodiscard]] Charset ResolveLoadCharset(const std::filesystem::path& path, std::string_view head,
                                         std::optional<Charset> chosen);

// How many of `head`'s bytes are `charset`'s preamble: all of it when the
// file starts with it, else none (a UTF-16 file stated without a BOM).
[[nodiscard]] std::size_t PreambleLength(std::string_view head, Charset charset);

// Process-wide: what a file's surroundings say it is encoded in. Consulted
// by ResolveLoadCharset only; unset, nothing is stated.
void SetStatedCharsetResolver(std::function<std::optional<Charset>(const std::filesystem::path&)> resolver);

// Decodes a file's bytes, preamble already stripped, into UTF-8, fed in
// pieces as they're read. The UTF-8 family passes bytes through untouched.
class CharsetDecoder {
  public:
    explicit CharsetDecoder(Charset charset);

    // Appends the decoded text of `bytes` to `out`; false once the input
    // can't be decoded (an unpaired UTF-16 surrogate), after which nothing
    // more is appended.
    [[nodiscard]] bool Feed(std::string_view bytes, std::string& out);

    // False if the input ended partway through a character.
    [[nodiscard]] bool Finish() const;

  private:
    Charset     charset_;
    std::string carry_; // an incomplete UTF-16 unit or surrogate pair
    bool        failed_ = false;
};

// A whole file's bytes as the text a load would show, going by its byte
// order mark alone (project-wide scans can't afford a stated charset per
// file): the mark stripped and UTF-16 decoded, anything else unchanged.
// nullopt when it doesn't decode.
[[nodiscard]] std::optional<std::string> DecodeAnnouncedCharset(std::string bytes);

// One-shot CharsetDecoder; nullopt if `bytes` don't decode.
[[nodiscard]] std::optional<std::string> DecodeCharset(std::string_view bytes, Charset charset);

// Encodes UTF-8 text into a file's charset, fed in pieces as they're
// written. The UTF-8 family passes bytes through untouched, invalid
// sequences included; any other charset refuses them.
class CharsetEncoder {
  public:
    explicit CharsetEncoder(Charset charset);

    // Appends the encoding of `utf8` to `out`; false once a character
    // can't be encoded, after which nothing more is appended.
    [[nodiscard]] bool Feed(std::string_view utf8, std::string& out);

    // False if the input ended partway through a UTF-8 sequence.
    [[nodiscard]] bool Finish() const;

    // Byte offset, into everything fed so far, of the character that
    // couldn't be encoded.
    [[nodiscard]] std::size_t FailedAt() const;

  private:
    Charset     charset_;
    std::string carry_; // an incomplete UTF-8 sequence
    std::size_t consumed_ = 0;
    bool        failed_   = false;
};

// One-shot CharsetEncoder; nullopt if some character can't be encoded.
[[nodiscard]] std::optional<std::string> EncodeCharset(std::string_view utf8, Charset charset);

} // namespace ned::text

#endif // NED_TEXT_CHARSET_H
