#include "SearchEngine.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
    // threads. Calls onFile with each searchable file, in walk order.
    template <typename OnFile>
    void WalkSearchableFiles(const std::filesystem::path& absoluteRoot, const std::stop_token& stop, OnFile onFile) {
        std::error_code ec;
        auto            it = std::filesystem::recursive_directory_iterator(
            absoluteRoot, std::filesystem::directory_options::skip_permission_denied, ec);
        const auto end = std::filesystem::recursive_directory_iterator();
        if (ec) {
            return;
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

            onFile(entry.path());
        }
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

    // The per-file scan across a small worker pool, fed while the walk is
    // still finding files. Workers take files in push order; a file's
    // matches go to the sink once it and every file before it are done, so
    // the result doesn't depend on scheduling.
    class ScanPipeline {
      public:
        ScanPipeline(const SearchPattern& pattern, unsigned int threads, const SearchSink& onMatches,
                     const std::stop_token& stop) : pattern_(pattern), onMatches_(onMatches), stop_(stop) {
            const unsigned int hardware = std::thread::hardware_concurrency();
            const unsigned int count    = std::max(1u, std::min(threads, hardware == 0 ? 4u : hardware));
            // The calling thread scans too once it's done pushing, but at
            // least one worker runs beside it so results flow during the walk.
            const unsigned int workers = std::max(1u, count - 1);
            workers_.reserve(workers);
            for (unsigned int t = 0; t < workers; ++t) {
                workers_.emplace_back([this] { Work(); });
            }
        }

        ~ScanPipeline() {
            Finish();
        }

        ScanPipeline(const ScanPipeline&)            = delete;
        ScanPipeline& operator=(const ScanPipeline&) = delete;

        // A non-null `live` is searched in place of reading the file.
        void Push(std::filesystem::path path, std::optional<text::Charset> stated, const LiveContent* live) {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                items_.push_back(Item{.path = std::move(path), .stated = stated, .live = live});
            }
            ready_.notify_one();
        }

        // Ends the input, scans beside the workers until nothing is left,
        // and waits for them.
        void Finish() {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (closed_) {
                    return;
                }
                closed_ = true;
            }
            ready_.notify_all();
            Work();
            workers_.clear();
        }

      private:
        struct Item {
            std::filesystem::path        path;
            std::optional<text::Charset> stated;
            const LiveContent*           live = nullptr;
            std::vector<SearchMatch>     matches;
            bool                         done = false;
        };

        void Work() {
            for (;;) {
                Item* item = nullptr;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    ready_.wait(lock, stop_, [this] { return next_ < items_.size() || closed_; });
                    if (stop_.stop_requested() || next_ == items_.size()) {
                        return;
                    }
                    // A deque's push_back leaves references to its elements valid.
                    item = &items_[next_++];
                }
                Complete(*item, Scan(*item));
            }
        }

        [[nodiscard]] std::vector<SearchMatch> Scan(const Item& item) const {
            if (item.live == nullptr) {
                return SearchOneFile(item.path, item.stated, pattern_);
            }
            if (item.live->preScanned) {
                return *item.live->preScanned;
            }
            return SearchOneText(item.path, item.live->text, pattern_);
        }

        void Complete(Item& item, std::vector<SearchMatch> matches) {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                item.matches = std::move(matches);
                item.done    = true;
            }
            // Collecting and sending under one lock keeps runs in order.
            const std::lock_guard<std::mutex> emitLock(emitMutex_);
            std::vector<SearchMatch>          run;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                for (; emitted_ < items_.size() && items_[emitted_].done; ++emitted_) {
                    std::vector<SearchMatch> done = std::exchange(items_[emitted_].matches, {});
                    run.insert(run.end(), std::make_move_iterator(done.begin()), std::make_move_iterator(done.end()));
                }
            }
            if (!run.empty() && !stop_.stop_requested()) {
                onMatches_(std::move(run));
            }
        }

        const SearchPattern&   pattern_;
        const SearchSink&      onMatches_;
        std::stop_token        stop_;

        std::mutex                  mutex_;
        std::condition_variable_any ready_;
        std::deque<Item>            items_;
        std::size_t                 next_    = 0;
        std::size_t                 emitted_ = 0;
        bool                        closed_  = false;

        std::mutex                emitMutex_;
        std::vector<std::jthread> workers_; // declared last, so joined before the state they use goes
    };

    std::vector<SearchMatch> Collect(const std::function<void(const SearchSink&)>& search, const std::stop_token& stop) {
        std::vector<SearchMatch> matches;
        search([&matches](std::vector<SearchMatch> run) {
            matches.insert(matches.end(), std::make_move_iterator(run.begin()), std::make_move_iterator(run.end()));
        });
        if (stop.stop_requested()) {
            return {};
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

SearchOptions CurrentSearchOptions() {
    return SearchOptions{.threads      = static_cast<unsigned int>(std::max(1, ProjectSearchThreads())),
                         .editorConfig = EditorConfigEnabled()};
}

std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const std::string& pattern,
                                         const std::stop_token& stop) {
    return SearchDirectory(root, SearchPattern(pattern), {}, stop);
}

SearchRoot::SearchRoot(const std::filesystem::path& root) {
    std::error_code             ec;
    const std::filesystem::path absolute = std::filesystem::absolute(root, ec);
    if (ec) {
        return;
    }
    normalized_ = absolute.lexically_normal();
    std::error_code canonicalEc;
    canonical_ = std::filesystem::weakly_canonical(normalized_, canonicalEc);
    if (canonicalEc) {
        canonical_ = normalized_;
    }
}

std::optional<std::filesystem::path> SearchRoot::Relative(const std::filesystem::path& path,
                                                          const std::filesystem::path& canonicalPath) const {
    if (!Valid()) {
        return std::nullopt;
    }
    if (IsUnderRoot(path, normalized_)) {
        return path.lexically_relative(normalized_);
    }
    if (!canonicalPath.empty() && IsUnderRoot(canonicalPath, canonical_)) {
        return canonicalPath.lexically_relative(canonical_);
    }
    return std::nullopt;
}

std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const SearchPattern& pattern,
                                         std::span<const LiveContent> live, const std::stop_token& stop) {
    return Collect([&](const SearchSink& sink) { StreamSearchDirectory(root, pattern, live, CurrentSearchOptions(), sink, stop); },
                   stop);
}

void StreamSearchDirectory(const std::filesystem::path& root, const SearchPattern& pattern,
                           std::span<const LiveContent> live, const SearchOptions& options, const SearchSink& onMatches,
                           const std::stop_token& stop) {
    std::error_code             ec;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(root, ec);
    if (ec) {
        return;
    }
    const SearchRoot searchRoot(absoluteRoot);

    // Each entry under root, with the path it's reported as: root's own
    // spelling, whichever of the entry's spellings put it there.
    struct InRoot {
        const LiveContent*    entry;
        std::filesystem::path relative;
        std::filesystem::path reported;
    };
    std::vector<InRoot> inRoot;
    for (const LiveContent& entry : live) {
        if (std::optional<std::filesystem::path> relative = searchRoot.Relative(entry.path, entry.canonicalPath)) {
            std::filesystem::path reported = (searchRoot.Normalized() / *relative).lexically_normal();
            inRoot.push_back(InRoot{&entry, std::move(*relative), std::move(reported)});
        }
    }
    // Deterministic order for whatever ends up appended after the walk.
    std::sort(inRoot.begin(), inRoot.end(), [](const InRoot& a, const InRoot& b) { return a.reported < b.reported; });

    std::vector<const LiveContent*> entries;
    entries.reserve(inRoot.size());
    for (const InRoot& item : inRoot) {
        entries.push_back(item.entry);
    }
    const std::unordered_map<std::string, const LiveContent*> byKey = LiveByKey(entries);

    // A walked file's canonical spelling is derived lexically from the
    // root's, not by a syscall per file.
    const bool rootAliased = searchRoot.Canonical() != searchRoot.Normalized();
    const auto findLive    = [&](const std::filesystem::path& walked) -> const LiveContent* {
        if (byKey.empty()) {
            return nullptr;
        }
        const std::filesystem::path normal = walked.lexically_normal();
        if (const auto it = byKey.find(normal.string()); it != byKey.end()) {
            return it->second;
        }
        if (!rootAliased) {
            return nullptr;
        }
        const std::filesystem::path alias =
            (searchRoot.Canonical() / normal.lexically_relative(searchRoot.Normalized())).lexically_normal();
        const auto it = byKey.find(alias.string());
        return it != byKey.end() ? it->second : nullptr;
    };

    EditorConfigCharsetCache               charsets(options.editorConfig);
    std::unordered_set<const LiveContent*> matchedByWalk;
    ScanPipeline                           pipeline(pattern, options.threads, onMatches, stop);
    WalkSearchableFiles(absoluteRoot, stop, [&](const std::filesystem::path& file) {
        const LiveContent* entry = findLive(file);
        if (entry != nullptr) {
            matchedByWalk.insert(entry);
        }
        pipeline.Push(file, entry != nullptr ? std::nullopt : charsets(file), entry);
    });

    // Live content the walk never saw, filtered by the walk's own rules so an
    // unsaved file under build/ doesn't show up where its saved twin wouldn't.
    if (!inRoot.empty()) {
        const GitIgnoreMatcher& gitIgnore = CachedGitIgnoreMatcher(absoluteRoot);
        for (const InRoot& item : inRoot) {
            if (!matchedByWalk.contains(item.entry) && !IsHiddenOrIgnored(item.relative, gitIgnore)) {
                pipeline.Push(item.reported, std::nullopt, item.entry);
            }
        }
    }
    pipeline.Finish();
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

    if (resolved.empty()) {
        return {};
    }
    const SearchOptions options = CurrentSearchOptions();
    return Collect(
        [&](const SearchSink& sink) {
            EditorConfigCharsetCache charsets(options.editorConfig);
            ScanPipeline             pipeline(pattern, static_cast<unsigned int>(std::min<std::size_t>(options.threads, resolved.size())), sink, {});
            for (std::size_t i = 0; i < resolved.size(); ++i) {
                pipeline.Push(resolved[i], sources[i] != nullptr ? std::nullopt : charsets(resolved[i]), sources[i]);
            }
            pipeline.Finish();
        },
        {});
}

} // namespace ned::editor
