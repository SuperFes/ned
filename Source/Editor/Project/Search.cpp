#include "Search.h"

#include <algorithm>
#include <memory>

#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor {

namespace {

    // Line-chunked scan over a buffer's own storage, materializing at most
    // kLinesPerChunk lines at a time. Chunk boundaries are line boundaries,
    // and project search is line-bounded, so no match can straddle one.
    std::vector<SearchMatch> SearchHugeStorage(const std::filesystem::path& path, const text::ITextStorage& content,
                                               const SearchPattern& pattern) {
        constexpr std::size_t kLinesPerChunk = 4096;

        std::vector<SearchMatch> matches;
        const std::size_t        lineCount = content.LineCount();
        for (std::size_t firstLine = 0; firstLine < lineCount; firstLine += kLinesPerChunk) {
            const std::size_t lastLine = std::min(firstLine + kLinesPerChunk, lineCount);
            const std::size_t start    = content.LineToByteOffset(firstLine);
            // lastLine == lineCount on the final chunk is fine: both storages
            // clamp a line index at or past LineCount() to ByteLength().
            const std::size_t end   = content.LineToByteOffset(lastLine);
            const std::string chunk = content.Substring(start, end - start);

            std::size_t lineNumber = firstLine; // 0-indexed here, +1 when recorded
            std::size_t pos        = 0;
            while (pos < chunk.size()) {
                const std::size_t eol     = chunk.find('\n', pos);
                const std::size_t lineEnd = (eol == std::string::npos) ? chunk.size() : eol;
                const std::string line    = chunk.substr(pos, lineEnd - pos);
                if (pattern.Matches(line)) {
                    matches.push_back(SearchMatch{path, lineNumber + 1, line});
                }
                ++lineNumber;
                pos = (eol == std::string::npos) ? chunk.size() : eol + 1;
            }
        }
        return matches;
    }

    // Every modified buffer with a path that `wanted(absolute, canonical)`
    // accepts. A huge one is scanned here rather than copied out whole.
    template <typename Wanted>
    std::vector<LiveContent> SnapshotModifiedBuffers(text::BufferList& bufferList, const SearchPattern& pattern,
                                                     Wanted wanted) {
        std::vector<LiveContent> live;
        for (const std::unique_ptr<text::Buffer>& buffer : bufferList.Buffers()) {
            if (buffer == nullptr || !buffer->Modified() || !buffer->Path()) {
                continue; // unmodified matches its file byte-for-byte; pathless has no file to stand in for
            }
            std::error_code             ec;
            const std::filesystem::path absolutePath = std::filesystem::absolute(*buffer->Path(), ec).lexically_normal();
            if (ec) {
                continue;
            }
            std::error_code       canonicalEc;
            std::filesystem::path canonical = std::filesystem::weakly_canonical(absolutePath, canonicalEc);
            if (canonicalEc) {
                canonical = absolutePath;
            }
            if (!wanted(absolutePath, canonical)) {
                continue;
            }
            LiveContent entry{absolutePath, std::move(canonical), {}, std::nullopt};
            if (buffer->Content().IsHuge()) {
                entry.preScanned = SearchHugeStorage(absolutePath, buffer->Content(), pattern);
            }
            else {
                entry.text = buffer->Text();
            }
            live.push_back(std::move(entry));
        }
        return live;
    }

} // namespace

std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const std::string& pattern,
                                         text::BufferList& liveBuffers) {
    const SearchPattern compiled(pattern);

    const SearchRoot searchRoot(root);
    if (!searchRoot.Valid()) {
        return {};
    }

    const std::vector<LiveContent> live = SnapshotModifiedBuffers(
        liveBuffers, compiled, [&searchRoot](const std::filesystem::path& absolutePath, const std::filesystem::path& canonical) {
            return searchRoot.Relative(absolutePath, canonical).has_value();
        });
    return SearchDirectory(root, compiled, live);
}

std::vector<SearchMatch> SearchFiles(const std::vector<std::filesystem::path>& files, const std::string& pattern,
                                     text::BufferList& liveBuffers) {
    const SearchPattern compiled(pattern);

    // Compared the way BufferList::FindByPath does, by canonical path.
    std::vector<std::filesystem::path> wantedKeys;
    for (const std::filesystem::path& file : files) {
        std::error_code             ec;
        const std::filesystem::path absolutePath = std::filesystem::absolute(file, ec).lexically_normal();
        if (ec) {
            continue;
        }
        std::error_code             canonicalEc;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolutePath, canonicalEc);
        wantedKeys.push_back(canonicalEc ? absolutePath : canonical);
    }

    const std::vector<LiveContent> live = SnapshotModifiedBuffers(
        liveBuffers, compiled, [&wantedKeys](const std::filesystem::path&, const std::filesystem::path& canonical) {
            return std::find(wantedKeys.begin(), wantedKeys.end(), canonical) != wantedKeys.end();
        });
    return SearchFiles(files, compiled, live);
}

} // namespace ned::editor
