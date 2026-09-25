#include "CaseCheck.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "Editor/EditorConfig.h"
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
    std::string ReadContent(text::BufferList* bufferList, const std::filesystem::path& path, std::optional<text::Charset> stated) {
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
        return text::DecodeAnnouncedCharset(contents.str(), stated).value_or(std::string());
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

    std::vector<std::filesystem::path> files;
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
        files.push_back(entry.path());
    }

    const std::vector<std::optional<text::Charset>> stated = EditorConfigCharsets(files);
    for (std::size_t i = 0; i < files.size(); ++i) {
        const std::string text = ReadContent(bufferList, files[i], stated[i]);
        if (text.empty()) {
            continue;
        }
        const Mode        mode        = ModeForPath(files[i]);
        const std::string languageKey = LanguageKeyForMode(mode);
        for (CaseViolation& violation : ComputeCaseViolations(text, languageKey, mode)) {
            const std::size_t line = LineNumberForByte(text, violation.nameStartByte);
            violations.push_back(ProjectCaseViolation{files[i], line, std::move(violation)});
        }
    }

    return violations;
}

} // namespace ned::editor
