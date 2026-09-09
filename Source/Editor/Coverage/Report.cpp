#include "Report.h"

#include <system_error>

namespace ned::editor::coverage {

namespace {

    // BufferList.cpp's own NormalizedPathKey, duplicated rather than shared
    // (small-helper duplication across translation units is this
    // codebase's own stated precedent -- see ValgrindOutputParser.cpp's own
    // comment on Trim/SplitLines).
    std::filesystem::path NormalizedPathKey(const std::filesystem::path& path) {
        std::error_code             ec;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        return ec ? std::filesystem::absolute(path) : canonical;
    }

} // namespace

const FileCoverage* FindFileCoverage(const Report& report, const std::filesystem::path& bufferPath,
                                     const std::filesystem::path& projectRoot) {
    const std::filesystem::path bufferKey = NormalizedPathKey(bufferPath);

    for (const FileCoverage& file : report) {
        const std::filesystem::path sfPath   = file.path;
        const std::filesystem::path resolved = sfPath.is_absolute() ? sfPath : (projectRoot / sfPath);
        if (NormalizedPathKey(resolved) == bufferKey) {
            return &file;
        }
    }

    const std::filesystem::path bufferFilename = bufferPath.filename();
    const FileCoverage*         uniqueMatch    = nullptr;
    for (const FileCoverage& file : report) {
        if (std::filesystem::path(file.path).filename() == bufferFilename) {
            if (uniqueMatch != nullptr) {
                return nullptr; // ambiguous -- more than one file shares this basename
            }
            uniqueMatch = &file;
        }
    }
    return uniqueMatch;
}

} // namespace ned::editor::coverage
