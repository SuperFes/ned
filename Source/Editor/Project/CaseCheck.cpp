#include "CaseCheck.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "Editor/GitIgnore.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Text/BinaryDetect.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor {

namespace {

    bool IsDotDirectory(const std::filesystem::directory_entry& entry) {
        const std::string name = entry.path().filename().string();
        return !name.empty() && name.front() == '.';
    }

    bool LooksHuge(text::BufferList* bufferList, const std::filesystem::path& path) {
        if (bufferList) {
            if (const text::Buffer* open = bufferList->FindByPath(path)) {
                return open->Content().IsHuge();
            }
        }
        std::error_code      ec;
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        return !ec && size > text::HugeFileThreshold();
    }

    // Live-over-disk: an open, modified, non-huge buffer's own text is what
    // the user is actually looking at, so a case scan should report against
    // that rather than the stale content still on disk.
    std::string ReadContent(text::BufferList* bufferList, const std::filesystem::path& path) {
        if (bufferList) {
            if (text::Buffer* open = bufferList->FindByPath(path)) {
                if (open->Modified() && !open->Content().IsHuge()) {
                    return open->Content().Substring(0, open->Content().ByteLength());
                }
            }
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

    std::size_t LineNumberForByte(const std::string& text, std::size_t byteOffset) {
        std::size_t line = 1;
        const std::size_t bound = std::min(byteOffset, text.size());
        for (std::size_t i = 0; i < bound; ++i) {
            if (text[i] == '\n') {
                ++line;
            }
        }
        return line;
    }

} // namespace

std::vector<ProjectCaseViolation> CollectProjectCaseViolations(const std::filesystem::path& root, text::BufferList* bufferList) {
    std::vector<ProjectCaseViolation> violations;

    std::error_code ec;
    auto            it = std::filesystem::recursive_directory_iterator(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const auto end = std::filesystem::recursive_directory_iterator();
    if (ec) {
        return violations;
    }

    const GitIgnoreMatcher& gitIgnore = CachedGitIgnoreMatcher(root);

    for (; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }

        const std::filesystem::directory_entry& entry    = *it;
        const std::filesystem::path             relative = std::filesystem::relative(entry.path(), root);

        if (entry.is_directory()) {
            if (IsDotDirectory(entry) || gitIgnore.IsIgnored(relative, /*isDirectory=*/true)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!entry.is_regular_file() || gitIgnore.IsIgnored(relative, /*isDirectory=*/false) ||
            text::LooksBinary(entry.path())) {
            continue;
        }
        if (LooksHuge(bufferList, entry.path())) {
            continue; // second-class throughout this codebase -- see this header's own comment
        }

        const Mode mode = ModeForPath(entry.path());
        if (!mode.localScopes && !mode.symbolKind) {
            continue; // nothing ComputeCaseViolations could ever report for this file
        }

        const std::string text = ReadContent(bufferList, entry.path());
        if (text.empty()) {
            continue;
        }
        const std::string languageKey = LanguageKeyForMode(mode);
        for (CaseViolation& violation : ComputeCaseViolations(text, languageKey, mode)) {
            const std::size_t line = LineNumberForByte(text, violation.nameStartByte);
            violations.push_back(ProjectCaseViolation{entry.path(), line, std::move(violation)});
        }
    }

    return violations;
}

} // namespace ned::editor
