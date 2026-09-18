#include "CliFormatDispatch.h"

#include <filesystem>

namespace ned::editor {

bool InvokedAsNedFormat(std::string_view argv0) {
    return std::filesystem::path(argv0).filename() == "ned-format";
}

bool InvokedAsNedLangc(std::string_view argv0) {
    return std::filesystem::path(argv0).filename() == "ned-langc";
}

} // namespace ned::editor
