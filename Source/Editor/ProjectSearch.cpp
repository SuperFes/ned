#include "ProjectSearch.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <thread>

#include <re2/re2.h>

#include "GitIgnore.h"
#include "SearchSettings.h"
#include "Text/BinaryDetect.h"

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
            const std::filesystem::path              relative = std::filesystem::relative(entry.path(), absoluteRoot);

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

    // Fans the per-file scan out across a small worker pool, one atomic
    // work-stealing counter deciding which file each thread picks up next
    // (better load-balancing than a static file-count/N split -- file sizes
    // vary a lot in a real project). Each file's own matches land in
    // perFile[i], written by exactly one thread each -- no cross-thread
    // contention -- then flattened back into a single vector in original
    // file order once every worker has finished, so the result is
    // deterministic regardless of which thread happened to process which
    // file.
    std::vector<SearchMatch> SearchFilesParallel(const std::vector<std::filesystem::path>& files, const re2::RE2& regex) {
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
                perFile[i] = SearchOneFile(files[i], regex);
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

    const std::vector<std::filesystem::path> files = CollectSearchableFiles(absoluteRoot);
    return SearchFilesParallel(files, regex);
}

} // namespace ned::editor
