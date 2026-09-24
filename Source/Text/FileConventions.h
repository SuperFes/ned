//
// Conventions a file's project states for it (.editorconfig) that override
// ned's own process-wide settings for that one buffer. Every field left
// unset defers to the global setting.
//

#ifndef NED_TEXT_FILECONVENTIONS_H
#define NED_TEXT_FILECONVENTIONS_H

#include <optional>

#include "Charset.h"
#include "LineEnding.h"

namespace ned::text {

struct FileConventions {
    std::optional<bool>       ensureFinalNewline;
    std::optional<bool>       trimTrailingWhitespace;
    std::optional<LineEnding> lineEnding; // what a save writes, ahead of the line-ending policy
    std::optional<Charset>    charset;    // what a load decodes and a save encodes, unless the user chose one
    std::optional<int>        maxLineLength; // the ruler and fill column; 0 means no limit

    [[nodiscard]] bool operator==(const FileConventions& other) const = default;
};

} // namespace ned::text

#endif // NED_TEXT_FILECONVENTIONS_H
