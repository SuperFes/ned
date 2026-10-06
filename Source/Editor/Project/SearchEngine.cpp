#include "SearchEngine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <iterator>
#include <thread>
#include <unordered_map>

#include <re2/re2.h>

#include "Editor/EditorConfig.h"
#include "Editor/GitIgnore.h"
#include "Editor/SearchSettings.h"
#include "Text/BinaryDetect.h"
#include "Text/Charset.h"

namespace ned::editor {

namespace {

    bool IsDotDirectory(const std::filesystem::directory_entry& entry) {
        const std::string name = entry.path().filename().string();
        return !name.empty() && name.front() == '.';
    }

    // Single-threaded: a recursive_directory_iterator can't be shared across
    // threads, and the walk was never the slow part.
    std::vector<std::filesystem::path> CollectSearchableFiles(const std::filesystem::path& absoluteRoot,
                                                              const std::stop_token&       stop) {
        std::vector<std::filesystem::path> files;

        std::error_code ec;
        auto            it = std::filesystem::recursive_directory_iterator(
            absoluteRoot, std::filesystem::directory_options::skip_permission_denied, ec);
        const auto end = std::filesystem::recursive_directory_iterator();
        if (ec) {
            return files;
        }

        const GitIgnoreMatcher& gitIgnore = CachedGitIgnoreMatcher(absoluteRoot);

        for (; it != end; it.increment(ec)) {
            if (ec || stop.stop_requested()) {
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

    // Whether p is at or under root, decided lexically (no syscalls, and no
    // opinion about whether p exists -- a never-saved buffer's path doesn't).
    bool IsUnderRoot(const std::filesystem::path& p, const std::filesystem::path& root) {
        const std::filesystem::path relative = p.lexically_relative(root);
        return !relative.empty() && *relative.begin() != "..";
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

    // Matches SearchOneFile's line semantics exactly -- std::getline splits on
    // '\n' and keeps a CRLF file's '\r', and a final line with no trailing
    // newline still counts -- so a match's lineText doesn't depend on whether
    // the file happened to be open.
    std::vector<SearchMatch> SearchOneText(const std::filesystem::path& path, const std::string& text,
                                           const SearchPattern& pattern) {
        std::vector<SearchMatch> matches;

        std::size_t lineNumber = 0;
        std::size_t pos        = 0;
        while (pos < text.size()) {
            const std::size_t eol     = text.find('\n', pos);
            const std::size_t lineEnd = (eol == std::string::npos) ? text.size() : eol;
            ++lineNumber;
            const std::string line = text.substr(pos, lineEnd - pos);
            if (pattern.Matches(line)) {
                matches.push_back(SearchMatch{path, lineNumber, line});
            }
            pos = (eol == std::string::npos) ? text.size() : eol + 1;
        }

        return matches;
    }

    // `stated` is the charset the file's surroundings (.editorconfig) say
    // it's in; its own byte order mark still wins.
    std::vector<SearchMatch> SearchOneFile(const std::filesystem::path& path, std::optional<text::Charset> stated,
                                           const SearchPattern& pattern) {
        std::vector<SearchMatch> matches;

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return matches;
        }

        // A file that isn't UTF-8 is searched as the text it holds, not its bytes.
        std::array<char, 3> head{};
        file.read(head.data(), head.size());
        const text::Charset sniffed = text::SniffCharset(std::string_view(head.data(), static_cast<std::size_t>(file.gcount())));
        file.clear();
        file.seekg(0);
        const text::Charset charset = (sniffed == text::Charset::Utf8 && stated) ? *stated : sniffed;
        if (!text::IsUtf8Family(charset)) {
            std::string                      bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            const std::optional<std::string> decoded = text::DecodeAnnouncedCharset(std::move(bytes), stated);
            return decoded ? SearchOneText(path, *decoded, pattern) : matches;
        }

        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(file, line)) {
            ++lineNumber;
            if (pattern.Matches(line)) {
                matches.push_back(SearchMatch{path, lineNumber, line});
            }
        }

        return matches;
    }

    // Fans the per-file scan out across a small worker pool, an atomic
    // counter handing each thread its next file (file sizes vary too much for
    // a static split). perFile[i] is written by exactly one thread, then
    // flattened in file order once every worker has joined, so the result
    // doesn't depend on scheduling. `live` is parallel to `files`: a non-null
    // entry is searched in place of reading that file.
    std::vector<SearchMatch> SearchFilesParallel(const std::vector<std::filesystem::path>& files,
                                                 const std::vector<const LiveContent*>&    live,
                                                 const SearchPattern& pattern, const std::stop_token& stop) {
        if (files.empty()) {
            return {};
        }

        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        const unsigned int threadCount     = std::max(
            1u, std::min({static_cast<unsigned int>(ProjectSearchThreads()), hardwareThreads == 0 ? 4u : hardwareThreads,
                          static_cast<unsigned int>(files.size())}));

        // Resolved before any worker exists: it reads .editorconfig files.
        const std::vector<std::optional<text::Charset>> stated = EditorConfigCharsets(files);

        std::vector<std::vector<SearchMatch>> perFile(files.size());
        std::atomic<std::size_t>              nextIndex{0};

        auto worker = [&]() {
            for (;;) {
                const std::size_t i = nextIndex.fetch_add(1, std::memory_order_relaxed);
                if (i >= files.size() || stop.stop_requested()) {
                    return;
                }
                if (live[i] == nullptr) {
                    perFile[i] = SearchOneFile(files[i], stated[i], pattern);
                }
                else if (live[i]->preScanned) {
                    perFile[i] = *live[i]->preScanned;
                }
                else {
                    perFile[i] = SearchOneText(files[i], live[i]->text, pattern);
                }
            }
        };

        {
            // The calling thread runs the same loop rather than idling.
            std::vector<std::jthread> workers;
            workers.reserve(threadCount - 1);
            for (unsigned int t = 1; t < threadCount; ++t) {
                workers.emplace_back(worker);
            }
            worker();
        }
        if (stop.stop_requested()) {
            return {};
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

    // Both spellings of each entry, so a file reached through either is
    // searched once, as live content, rather than twice.
    std::unordered_map<std::string, const LiveContent*> LiveByKey(std::span<const LiveContent* const> live) {
        std::unordered_map<std::string, const LiveContent*> byKey;
        for (const LiveContent* entry : live) {
            byKey.emplace(entry->path.string(), entry);
            byKey.emplace(entry->canonicalPath.string(), entry);
        }
        return byKey;
    }

} // namespace

SearchPattern::SearchPattern(const std::string& pattern) {
    re2::RE2::Options options;
    // RE2 logs a parse failure to stderr by default, which would corrupt the
    // terminal; the diagnostic goes through SearchPatternError instead.
    options.set_log_errors(false);
    regex_ = std::make_unique<re2::RE2>(pattern, options);
    if (!regex_->ok()) {
        throw SearchPatternError(regex_->error());
    }
}

SearchPattern::~SearchPattern()                                   = default;
SearchPattern::SearchPattern(SearchPattern&&) noexcept            = default;
SearchPattern& SearchPattern::operator=(SearchPattern&&) noexcept = default;

bool SearchPattern::Matches(std::string_view line) const {
    return re2::RE2::PartialMatch(line, *regex_);
}

std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const std::string& pattern,
                                         const std::stop_token& stop) {
    return SearchDirectory(root, SearchPattern(pattern), {}, stop);
}

std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const SearchPattern& pattern,
                                         std::span<const LiveContent> live, const std::stop_token& stop) {
    std::error_code             ec;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(root, ec);
    if (ec) {
        return {};
    }
    const std::filesystem::path normalizedRoot = absoluteRoot.lexically_normal();

    std::vector<const LiveContent*> inRoot;
    for (const LiveContent& entry : live) {
        if (IsUnderRoot(entry.path, normalizedRoot)) {
            inRoot.push_back(&entry);
        }
    }
    // Deterministic order for whatever ends up appended after the walk.
    std::sort(inRoot.begin(), inRoot.end(), [](const LiveContent* a, const LiveContent* b) { return a->path < b->path; });

    std::vector<std::filesystem::path> files = CollectSearchableFiles(absoluteRoot, stop);
    std::vector<const LiveContent*>    sources(files.size(), nullptr);

    if (!inRoot.empty()) {
        const std::unordered_map<std::string, const LiveContent*> byKey = LiveByKey(inRoot);

        std::vector<const LiveContent*> matchedByWalk;
        for (std::size_t i = 0; i < files.size(); ++i) {
            const auto it = byKey.find(files[i].lexically_normal().string());
            if (it != byKey.end()) {
                sources[i] = it->second;
                matchedByWalk.push_back(it->second);
            }
        }

        // Live content the walk never saw, filtered by the walk's own rules
        // so an unsaved file under build/ doesn't show up where its saved
        // twin wouldn't.
        const GitIgnoreMatcher& gitIgnore = CachedGitIgnoreMatcher(absoluteRoot);
        for (const LiveContent* entry : inRoot) {
            if (std::find(matchedByWalk.begin(), matchedByWalk.end(), entry) != matchedByWalk.end()) {
                continue;
            }
            if (IsHiddenOrIgnored(entry->path.lexically_relative(normalizedRoot), gitIgnore)) {
                continue;
            }
            files.push_back(entry->path);
            sources.push_back(entry);
        }
    }

    return SearchFilesParallel(files, sources, pattern, stop);
}

std::vector<SearchMatch> SearchFiles(const std::vector<std::filesystem::path>& files, const SearchPattern& pattern,
                                     std::span<const LiveContent> live) {
    std::vector<const LiveContent*> entries;
    entries.reserve(live.size());
    for (const LiveContent& entry : live) {
        entries.push_back(&entry);
    }
    const std::unordered_map<std::string, const LiveContent*> byKey = LiveByKey(entries);

    const auto findLive = [&byKey](const std::filesystem::path& absolutePath) -> const LiveContent* {
        if (byKey.empty()) {
            return nullptr;
        }
        if (const auto it = byKey.find(absolutePath.string()); it != byKey.end()) {
            return it->second;
        }
        std::error_code             ec;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolutePath, ec);
        if (ec) {
            return nullptr;
        }
        const auto it = byKey.find(canonical.string());
        return it != byKey.end() ? it->second : nullptr;
    };

    std::vector<std::filesystem::path> resolved;
    std::vector<const LiveContent*>    sources;
    for (const std::filesystem::path& file : files) {
        std::error_code             ec;
        const std::filesystem::path absolutePath = std::filesystem::absolute(file, ec).lexically_normal();
        if (ec) {
            continue;
        }
        if (std::find(resolved.begin(), resolved.end(), absolutePath) != resolved.end()) {
            continue;
        }
        // Live content for a never-saved file is as good a source as the file.
        const LiveContent* source = findLive(absolutePath);
        std::error_code    existsEc;
        if (source == nullptr && !std::filesystem::exists(absolutePath, existsEc)) {
            continue;
        }
        resolved.push_back(absolutePath);
        sources.push_back(source);
    }

    return SearchFilesParallel(resolved, sources, pattern, {});
}

} // namespace ned::editor
