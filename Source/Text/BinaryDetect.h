//
// Cheap binary-content sniffing, shared by ProjectSearch (skip binary files
// when scanning a project) and Buffer::FromFile (refuse to load a binary
// file as text at all -- see that function's own comment for why).
//

#ifndef NED_TEXT_BINARYDETECT_H
#define NED_TEXT_BINARYDETECT_H

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "Charset.h"

namespace ned::text {

// Reads up to the first 8KiB of path and reports whether it contains a NUL
// byte -- the same git/grep heuristic ProjectSearch originally used on its
// own -- except in a file whose byte order mark announces UTF-16, where
// NUL bytes are ordinary and it's a NUL character or a malformed unit that
// counts. An unreadable path counts as binary too (nothing useful to do
// with it either way). Never throws.
[[nodiscard]] bool LooksBinary(const std::filesystem::path& path);

// LooksBinary as a load decides it: judged in the charset the load would
// decode with (ResolveLoadCharset, `chosen` included), so a UTF-16 file
// .editorconfig states without a BOM isn't mistaken for binary.
[[nodiscard]] bool LooksBinaryToLoad(const std::filesystem::path& path, std::optional<Charset> chosen = std::nullopt);

// The judgement itself, over a file's first bytes.
[[nodiscard]] bool HeadLooksBinary(std::string_view head, Charset charset);

// open-binary-anyway follow-up: thrown by Buffer::FromFile/BufferList::
// OpenFile specifically for a LooksBinary refusal, distinct from the plain
// std::runtime_error every other I/O failure there throws (unreadable path,
// permissions, ...) -- lets a caller offer "open anyway?" only for this one
// specific, overridable case (an interactive y/n confirmation, a --force-
// binary CLI flag) without also offering to force through a real I/O error
// that a retry can't fix.
class BinaryFileError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

} // namespace ned::text

#endif // NED_TEXT_BINARYDETECT_H
