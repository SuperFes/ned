#include "Search.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <thread>

#include <re2/re2.h>

#include <memory>
#include <unordered_map>

#include "Editor/GitIgnore.h"
#include "Editor/SearchSettings.h"
#include "Text/BinaryDetect.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor {

namespace {

    bool IsDotDirectory(const std::filesystem::directory_entry& entry) {
        const std::string name = entry.path().filename().string();
        return !name.empty() && name.front() == '.';
    }

    // The directory-walk half -- unchanged in spirit from the old single-
    // threaded scanner's own walk, just no longer also doing the (now
    // parallelized) per-file line search inline. Single-threaded: a
    // recursive_directory_iterator has no thread-safe way to be shared
    // across threads, and the walk itself was never the slow part (reading
    // + regex-matching every line of every file was).
    std::vector<std::filesystem::path> CollectSearchableFiles(const std::filesystem::path& absoluteRoot) {
        std::vector<std::filesystem::path> files;

        std::error_code ec;
        auto            it = std::filesystem::recursive_directory_iterator(
            absoluteRoot, std::filesystem::directory_options::skip_permission_denied, ec);
        const auto end = std::filesystem::recursive_directory_iterator();
        if (ec) {
            return files;
        }

        // project-search-hang follow-up: see GitIgnore.h's own header
        // comment for why this exists at all; project-search-rg-removal
        // follow-up: cached rather than reparsed on every search.
        const GitIgnoreMatcher& gitIgnore = CachedGitIgnoreMatcher(absoluteRoot);

        for (; it != end; it.increment(ec)) {
            if (ec) {
                break;
            }

            const std::filesystem::directory_entry& entry    = *it;
            const std::filesystem::path             relative = std::filesystem::relative(entry.path(), absoluteRoot);

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

            files.push_back(entry.path());
        }

        return files;
    }

    // live-buffer-search follow-up: one modified buffer's content, plus the
    // path keys a walked file is matched against. Two keys because a buffer
    // was opened through whatever path the user typed (often relative to the
    // cwd) while the walk builds its own absolute paths from the root: the
    // lexically-normalized absolute form covers the ordinary case, the
    // canonical form covers a symlinked root or path. Matching on either is
    // what keeps a file from being searched twice, once as disk and once as
    // live.
    struct LiveFile {
        std::filesystem::path path; // absolute -- reported as SearchMatch::file
        std::string           text; // empty for a huge buffer -- see preScanned
        std::filesystem::path normalizedKey;
        std::filesystem::path canonicalKey;
        // huge-live-buffer follow-up: a huge buffer is never snapshotted
        // whole. It's scanned line-chunk by line-chunk instead, on the
        // calling thread (its ITextStorage is read concurrently by nobody,
        // which is the point), and the result carried here for the worker
        // pool to hand back in place of a scan of its own.
        std::vector<SearchMatch> preScanned;
        bool                     huge = false;
    };

    // Line-chunked RE2 scan over a buffer's own storage, materializing at
    // most kLinesPerChunk lines at a time. Chunk boundaries are line
    // boundaries, so nothing can straddle one -- unlike HugeRegexScan.h's
    // window arithmetic, which exists because a PCRE2 match may span lines;
    // project search is line-bounded by construction.
    std::vector<SearchMatch> SearchHugeStorage(const std::filesystem::path& path, const text::ITextStorage& content,
                                               const re2::RE2& regex) {
        constexpr std::size_t kLinesPerChunk = 4096;

        std::vector<SearchMatch> matches;
        const std::size_t        lineCount = content.LineCount();
        for (std::size_t firstLine = 0; firstLine < lineCount; firstLine += kLinesPerChunk) {
            const std::size_t lastLine = std::min(firstLine + kLinesPerChunk, lineCount);
            const std::size_t start    = content.LineToByteOffset(firstLine);
            // lastLine == lineCount on the final chunk is fine: both storages
            // clamp a line index at or past LineCount() to ByteLength().
            const std::size_t end = content.LineToByteOffset(lastLine);
            const std::string chunk = content.Substring(start, end - start);

            std::size_t lineNumber = firstLine; // 0-indexed here, +1 when recorded
            std::size_t pos        = 0;
            while (pos < chunk.size()) {
                const std::size_t eol     = chunk.find('\n', pos);
                const std::size_t lineEnd = (eol == std::string::npos) ? chunk.size() : eol;
                const std::string line    = chunk.substr(pos, lineEnd - pos);
                if (re2::RE2::PartialMatch(line, regex)) {
                    matches.push_back(SearchMatch{path, lineNumber + 1, line});
                }
                ++lineNumber;
                pos = (eol == std::string::npos) ? chunk.size() : eol + 1;
            }
        }
        return matches;
    }

    // Whether p is at or under root, decided lexically (no syscalls, and no
    // opinion about whether p exists -- a never-saved buffer's path doesn't).
    bool IsUnderRoot(const std::filesystem::path& p, const std::filesystem::path& root) {
        const std::filesystem::path relative = p.lexically_relative(root);
        return !relative.empty() && *relative.begin() != "..";
    }

    std::vector<LiveFile> SnapshotModifiedBuffers(text::BufferList& bufferList, const std::filesystem::path& absoluteRoot,
                                                  const re2::RE2& regex) {
        std::vector<LiveFile> live;
        for (const std::unique_ptr<text::Buffer>& buffer : bufferList.Buffers()) {
            if (buffer == nullptr || !buffer->Modified() || !buffer->Path()) {
                continue; // unmodified matches its file byte-for-byte; pathless has no file to stand in for
            }
            std::error_code             ec;
            const std::filesystem::path absolutePath = std::filesystem::absolute(*buffer->Path(), ec).lexically_normal();
            if (ec || !IsUnderRoot(absolutePath, absoluteRoot)) {
                continue;
            }
            std::error_code       canonicalEc;
            std::filesystem::path canonical = std::filesystem::weakly_canonical(absolutePath, canonicalEc);
            if (canonicalEc) {
                canonical = absolutePath;
            }
            const bool huge = buffer->Content().IsHuge();
            live.push_back(LiveFile{absolutePath, huge ? std::string{} : buffer->Text(), absolutePath, std::move(canonical), {}, huge});
        }
        // Deterministic order for whatever ends up appended after the walk.
        std::sort(live.begin(), live.end(), [](const LiveFile& a, const LiveFile& b) { return a.path < b.path; });
        // Huge buffers are scanned here, before any worker exists -- see
        // LiveFile::preScanned. Second-class by design (single-threaded, and
        // it re-reads through the buffer's own line index rather than one
        // flat string), but correct: a huge buffer's unsaved edits are no
        // longer invisible to search.
        for (LiveFile& entry : live) {
            if (!entry.huge) {
                continue;
            }
            if (const text::Buffer* buffer = bufferList.FindByPath(entry.path)) {
                entry.preScanned = SearchHugeStorage(entry.path, buffer->Content(), regex);
            }
        }
        return live;
    }

    // The walk's own two skip rules (dot-directory, .gitignore), applied to a
    // path the walk never visited. Every ancestor directory has to be checked
    // as a directory in its own right, not just the file: the walk gets
    // "everything under build/ is ignored" for free by never descending into
    // it, whereas a `build/` pattern doesn't match the file `build/x.txt` on
    // its own terms at all.
    bool IsHiddenOrIgnored(const std::filesystem::path& relative, const GitIgnoreMatcher& gitIgnore) {
        std::filesystem::path prefix;
        for (const std::filesystem::path& part : relative) {
            const std::string name = part.string();
            if (name.empty()) {
                continue;
            }
            if (name.front() == '.') {
                return true;
            }
            prefix /= part;
            const bool isDirectory = (prefix != relative);
            if (gitIgnore.IsIgnored(prefix, isDirectory)) {
                return true;
            }
        }
        return false;
    }

    // One file's worth of line-by-line matching -- RE2 objects are safe for
    // concurrent use by multiple threads (its own documented contract; the
    // DFA state cache it builds up while matching guards itself internally),
    // so every worker below shares the one compiled `regex` rather than each
    // needing its own copy.
    std::vector<SearchMatch> SearchOneFile(const std::filesystem::path& path, const re2::RE2& regex) {
        std::vector<SearchMatch> matches;

        std::ifstream file(path);
        if (!file) {
            return matches;
        }

        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(file, line)) {
            ++lineNumber;
            if (re2::RE2::PartialMatch(line, regex)) {
                matches.push_back(SearchMatch{path, lineNumber, line});
            }
        }

        return matches;
    }

    // live-buffer-search follow-up: SearchOneFile's in-memory twin, matching
    // its line semantics exactly rather than approximately -- std::getline
    // splits on '\n' and keeps a CRLF file's '\r', and a final line with no
    // trailing newline still counts as a line. Anything else here would make
    // a match's own lineText differ depending on whether the file happened to
    // be open.
    std::vector<SearchMatch> SearchOneText(const std::filesystem::path& path, const std::string& text, const re2::RE2& regex) {
        std::vector<SearchMatch> matches;

        std::size_t lineNumber = 0;
        std::size_t pos        = 0;
        while (pos < text.size()) {
            const std::size_t eol     = text.find('\n', pos);
            const std::size_t lineEnd = (eol == std::string::npos) ? text.size() : eol;
            ++lineNumber;
            const std::string line = text.substr(pos, lineEnd - pos);
            if (re2::RE2::PartialMatch(line, regex)) {
                matches.push_back(SearchMatch{path, lineNumber, line});
            }
            pos = (eol == std::string::npos) ? text.size() : eol + 1;
        }

        return matches;
    }

    // Fans the per-file scan out across a small worker pool, one atomic
    // work-stealing counter deciding which file each thread picks up next
    // (better load-balancing than a static file-count/N split -- file sizes
    // vary a lot in a real project). Each file's own matches land in
    // perFile[i], written by exactly one thread each -- no cross-thread
    // contention -- then flattened back into a single vector in original
    // file order once every worker has finished, so the result is
    // deterministic regardless of which thread happened to process which
    // file.
    // liveText is parallel to files: a non-null entry is that file's live
    // buffer content, searched in place of reading the file at all.
    std::vector<SearchMatch> SearchFilesParallel(const std::vector<std::filesystem::path>&           files,
                                                 const std::vector<const std::string*>&              liveText,
                                                 const std::vector<const std::vector<SearchMatch>*>& preScanned,
                                                 const re2::RE2&                                     regex) {
        if (files.empty()) {
            return {};
        }

        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        const unsigned int threadCount     = std::max(
            1u, std::min({static_cast<unsigned int>(ProjectSearchThreads()), hardwareThreads == 0 ? 4u : hardwareThreads,
                          static_cast<unsigned int>(files.size())}));

        std::vector<std::vector<SearchMatch>> perFile(files.size());
        std::atomic<std::size_t>              nextIndex{0};

        auto worker = [&]() {
            for (;;) {
                const std::size_t i = nextIndex.fetch_add(1, std::memory_order_relaxed);
                if (i >= files.size()) {
                    return;
                }
                if (preScanned[i] != nullptr) {
                    perFile[i] = *preScanned[i]; // a huge live buffer, already scanned on the calling thread
                }
                else {
                    perFile[i] = (liveText[i] != nullptr) ? SearchOneText(files[i], *liveText[i], regex)
                                                          : SearchOneFile(files[i], regex);
                }
            }
        };

        {
            // threadCount - 1 background workers, plus the calling thread
            // itself running the same worker loop below -- never leaves the
            // calling thread idle while others do the work.
            std::vector<std::jthread> workers;
            workers.reserve(threadCount - 1);
            for (unsigned int t = 1; t < threadCount; ++t) {
                workers.emplace_back(worker);
            }
            worker();
            // workers' destructor joins every thread here, before perFile is
            // read below.
        }

        std::size_t total = 0;
        for (const std::vector<SearchMatch>& m : perFile) {
            total += m.size();
        }

        std::vector<SearchMatch> matches;
        matches.reserve(total);
        for (std::vector<SearchMatch>& m : perFile) {
            matches.insert(matches.end(), std::make_move_iterator(m.begin()), std::make_move_iterator(m.end()));
        }
        return matches;
    }

} // namespace

std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const std::string& pattern) {
    re2::RE2::Options options;
    // This is a live TUI app -- RE2's default logging on a parse failure
    // writes straight to stderr, which would corrupt the terminal display
    // the same way an inherited stderr fd from a child process would (see
    // this file's own history with rg's stderr for the same reason). The
    // diagnostic is surfaced through SearchPatternError::what() instead.
    options.set_log_errors(false);
    re2::RE2 regex(pattern, options);
    if (!regex.ok()) {
        throw SearchPatternError(regex.error());
    }

    std::error_code             ec;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(root, ec);
    if (ec) {
        return {};
    }

    std::vector<std::filesystem::path> files = CollectSearchableFiles(absoluteRoot);
    return SearchFilesParallel(files, std::vector<const std::string*>(files.size(), nullptr),
                               std::vector<const std::vector<SearchMatch>*>(files.size(), nullptr), regex);
}

std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const std::string& pattern,
                                         text::BufferList& liveBuffers) {
    re2::RE2::Options options;
    options.set_log_errors(false); // see the two-argument overload above
    re2::RE2 regex(pattern, options);
    if (!regex.ok()) {
        throw SearchPatternError(regex.error());
    }

    std::error_code             ec;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(root, ec);
    if (ec) {
        return {};
    }

    // Taken before any worker exists -- see the header's own threading note.
    const std::vector<LiveFile> live = SnapshotModifiedBuffers(liveBuffers, absoluteRoot.lexically_normal(), regex);

    std::vector<std::filesystem::path>           files = CollectSearchableFiles(absoluteRoot);
    std::vector<const std::string*>              liveText(files.size(), nullptr);
    std::vector<const std::vector<SearchMatch>*> preScanned(files.size(), nullptr);

    if (!live.empty()) {
        std::unordered_map<std::string, const LiveFile*> liveByKey;
        for (const LiveFile& entry : live) {
            liveByKey.emplace(entry.normalizedKey.string(), &entry);
            liveByKey.emplace(entry.canonicalKey.string(), &entry);
        }

        std::vector<const LiveFile*> matchedByWalk;
        for (std::size_t i = 0; i < files.size(); ++i) {
            const auto it = liveByKey.find(files[i].lexically_normal().string());
            if (it != liveByKey.end()) {
                if (it->second->huge) {
                    preScanned[i] = &it->second->preScanned;
                }
                else {
                    liveText[i] = &it->second->text;
                }
                matchedByWalk.push_back(it->second);
            }
        }

        // Anything the walk never saw -- a modified buffer for a file that
        // doesn't exist on disk yet. Appended in path order (the snapshot is
        // already sorted) so the result stays deterministic, and filtered by
        // the same .gitignore/dot-directory rules the walk applies, so an
        // unsaved buffer under build/ doesn't show up where its saved twin
        // wouldn't.
        const GitIgnoreMatcher& gitIgnore = CachedGitIgnoreMatcher(absoluteRoot);
        for (const LiveFile& entry : live) {
            if (std::find(matchedByWalk.begin(), matchedByWalk.end(), &entry) != matchedByWalk.end()) {
                continue;
            }
            if (IsHiddenOrIgnored(entry.path.lexically_relative(absoluteRoot.lexically_normal()), gitIgnore)) {
                continue;
            }
            files.push_back(entry.path);
            liveText.push_back(entry.huge ? nullptr : &entry.text);
            preScanned.push_back(entry.huge ? &entry.preScanned : nullptr);
        }
    }

    if (preScanned.size() < files.size()) {
        preScanned.resize(files.size(), nullptr);
    }
    return SearchFilesParallel(files, liveText, preScanned, regex);
}

} // namespace ned::editor
