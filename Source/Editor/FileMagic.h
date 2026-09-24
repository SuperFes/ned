//
// libmagic's MIME verdict on a file's leading bytes -- what file(1) says --
// for telling apart languages that share an extension (a `.h` that is C,
// C++ or Objective-C). One handle for the process, opened on first use and
// guarded by a mutex: libmagic handles are not thread-safe, and mode
// resolution also runs on the prewarm thread.
//

#ifndef NED_EDITOR_FILEMAGIC_H
#define NED_EDITOR_FILEMAGIC_H

#include <optional>
#include <string>
#include <string_view>

namespace ned::editor {

// e.g. "text/x-objective-c"; nullopt when libmagic could not load its
// database or has no answer.
[[nodiscard]] std::optional<std::string> MimeTypeOf(std::string_view bytes);

} // namespace ned::editor

#endif // NED_EDITOR_FILEMAGIC_H
